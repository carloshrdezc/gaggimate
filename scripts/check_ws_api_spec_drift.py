#!/usr/bin/env python3
"""Fail when the WebSocket message surface drifts from docs/websocket-api.yaml (PRO-610).

`docs/websocket-api.yaml` is the AsyncAPI 2.6.0 contract for the `/ws` protocol,
but nothing enforced it: `req:grinders:select` shipped in PRO-424 with a live web
caller and was simply never documented, and because responses are built
dynamically (`response["tp"] = String("res:") + type.substring(4);`) its
`res:grinders:select` reply was undocumented too. Nobody noticed for months
because no gate compared the three sides of the protocol. This script is that
gate.

It compares, in both directions:

  1. firmware `req:*` handlers  <->  `req:*` operations in the spec
  2. firmware `res:*` replies   <->  `res:*` operations in the spec
     (both the literal `"res:x"` assignments and the ones derived from the
     `String("res:") + type.substring(4)` handlers)
  3. web-client `req:*` senders <->  firmware `req:*` handlers

Direction 3 has an ALLOW-LIST: three firmware handlers are deliberate
legacy/HTTP-superseded orphans with no web caller. It is NOT duplicated here —
it is parsed out of the marker block in
``web/src/services/ApiService.contract.test.js``, which already recorded those
three in its PRO-16 audit note, so there is exactly one list to maintain.

No third-party dependencies on purpose (no PyYAML): the spec is parsed for the
`enum: ['req:...']` / `enum: ['res:...']` operation markers with a regex, so this
runs anywhere python3 does, including as a cheap first step in CI.

Usage:
    python3 scripts/check_ws_api_spec_drift.py          # exit 1 on any drift
"""
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SPEC = "docs/websocket-api.yaml"
FIRMWARE_DIR = "src/display/plugins"
WEB_SRC = "web/src"
CONTRACT_TEST = "web/src/services/ApiService.contract.test.js"

# THE allow-list of intentional spec/implementation asymmetries lives in
# CONTRACT_TEST, between these markers — one canonical list, not two. That file
# already recorded the same three handlers as deliberate legacy/HTTP-superseded
# orphans (PRO-16's audit note); PRO-610 promoted the note to a parseable block
# instead of inventing a second list here. It is a `.test.js` ESM module, so it
# cannot be imported from python; a marker-delimited comment block is the cheapest
# form both a JS reader and this gate can share.
_ALLOWLIST_BEGIN = "PRO-610-ALLOWLIST-BEGIN"
_ALLOWLIST_END = "PRO-610-ALLOWLIST-END"

# `req:*` handlers that intentionally have NO web-client caller. Not a literal:
# read from CONTRACT_TEST so the two can never disagree.
_ALLOWLIST_ENTRY_RE = re.compile(r"`(req:[a-z0-9:.-]+)`")

# `type == "req:x"` / `msgType == "req:x"` dispatch branches. Only `==`: the
# prefix routers (`msgType.startsWith("req:profiles:")`) and the one negative
# test (`msgType != "req:beans:select"`) are not handler declarations.
_HANDLER_RE = re.compile(r'==\s*"(req:[^"]+)"')

# Start of a top-level `Type Class::method(` definition — used to split a .cpp
# into per-function blocks so a handler can be attributed to the function it
# lives in (see derived_response_types).
_FUNCTION_START_RE = re.compile(r"^[A-Za-z_][^\n(]*\b\w+::\w+\s*\(", re.M)

# The dynamic reply-tp idiom: every `req:x` branch in a function containing this
# line answers with `res:x`.
_DYNAMIC_RESPONSE_TP = 'String("res:") + type.substring(4)'

# Literal reply assignments, e.g. `response["tp"] = "res:auth";`.
_LITERAL_RESPONSE_RE = re.compile(r'\["tp"\]\s*=\s*"(res:[^"]+)"')

# AsyncAPI operation markers: `enum: ['req:grinders:select']`.
_SPEC_OPERATION_RE = re.compile(r"enum:\s*\['((?:req|res):[^']+)'\]")

# `tp` string literals in the web client.
_CLIENT_TP_RE = re.compile(r"""['"](req:[a-z0-9:.-]+)['"]""")


def _read(rel_path):
    with open(os.path.join(PROJECT_ROOT, *rel_path.split("/")), encoding="utf-8") as fh:
        return fh.read()


def _sources(rel_dir, suffixes, skip_tests=False):
    """Yield (relative_path, text) for files under rel_dir with these suffixes."""
    root = os.path.join(PROJECT_ROOT, *rel_dir.split("/"))
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if not name.endswith(suffixes):
                continue
            if skip_tests and ".test." in name:
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, PROJECT_ROOT).replace("\\", "/")
            with open(path, encoding="utf-8") as fh:
                yield rel, fh.read()


def handler_types(source):
    """The `req:*` types a firmware translation unit dispatches on."""
    return set(_HANDLER_RE.findall(source))


def derived_response_types(source):
    """The `res:*` types a firmware translation unit can emit.

    Two shapes: the literal `["tp"] = "res:x"` assignments, and — for each
    function that builds its reply tp from the request tp — `res:x` for every
    `req:x` branch inside that function. The second shape is why
    `res:grinders:select` existed in the wire protocol without any string
    "res:grinders:select" appearing anywhere in the firmware or the spec.
    """
    out = set(_LITERAL_RESPONSE_RE.findall(source))
    bounds = [m.start() for m in _FUNCTION_START_RE.finditer(source)] + [len(source)]
    for start, end in zip(bounds, bounds[1:]):
        block = source[start:end]
        if _DYNAMIC_RESPONSE_TP not in block:
            continue
        for req in _HANDLER_RE.findall(block):
            out.add("res:" + req[len("req:") :])
    return out


def spec_operations(spec_text):
    """The documented operations, split into ({req:*}, {res:*})."""
    found = set(_SPEC_OPERATION_RE.findall(spec_text))
    return {op for op in found if op.startswith("req:")}, {op for op in found if op.startswith("res:")}


def client_request_types(files):
    """The `req:*` types the web client sends, from (path, text) pairs."""
    out = set()
    for _rel, text in files:
        out.update(_CLIENT_TP_RE.findall(text))
    return {tp for tp in out if tp != "req:"}


def clientless_handlers(contract_test_text=None):
    """Parse the canonical client-less-handler allow-list out of CONTRACT_TEST.

    Raises if the markers are missing or the block is empty: a silently-empty
    allow-list would turn every intentional orphan into a CI failure, and a
    silently-*ignored* block would let a real orphan through. Both are worse than
    a loud error naming the file.
    """
    text = _read(CONTRACT_TEST) if contract_test_text is None else contract_test_text
    try:
        block = text.split(_ALLOWLIST_BEGIN, 1)[1].split(_ALLOWLIST_END, 1)[0]
    except IndexError:
        raise SystemExit(
            f"{CONTRACT_TEST} is missing the {_ALLOWLIST_BEGIN}/{_ALLOWLIST_END} block that holds the "
            f"canonical client-less-handler allow-list (PRO-610). Restore it, or point this gate elsewhere."
        )
    entries = frozenset(_ALLOWLIST_ENTRY_RE.findall(block))
    if not entries:
        raise SystemExit(
            f"the {_ALLOWLIST_BEGIN} block in {CONTRACT_TEST} lists no `req:*` types (PRO-610). "
            f"Each entry must be a backtick-quoted req:* type on its own line."
        )
    return entries


def collect():
    """Gather the sets the gate compares from the real working tree."""
    firmware = list(_sources(FIRMWARE_DIR, (".cpp",)))
    handlers = set()
    responses = set()
    for _rel, text in firmware:
        handlers |= handler_types(text)
        responses |= derived_response_types(text)
    spec_req, spec_res = spec_operations(_read(SPEC))
    client = client_request_types(_sources(WEB_SRC, (".js", ".jsx"), skip_tests=True))
    return {
        "handlers": handlers,
        "responses": responses,
        "spec_req": spec_req,
        "spec_res": spec_res,
        "client": client,
    }


def check(sets, allow_clientless):
    """Return a list of human-readable drift problems (empty == green)."""
    problems = []

    undocumented = sorted(sets["handlers"] - sets["spec_req"])
    if undocumented:
        problems.append(
            f"{len(undocumented)} firmware req:* handler(s) are missing from {SPEC}: {undocumented}. "
            f"Add a <Name>Request message (modelled on a sibling, e.g. BeansSelectRequest) plus a "
            f"$ref under channels['/ws'].publish."
        )

    phantom = sorted(sets["spec_req"] - sets["handlers"])
    if phantom:
        problems.append(
            f"{len(phantom)} req:* operation(s) in {SPEC} have no firmware handler in {FIRMWARE_DIR}/*.cpp: "
            f"{phantom}. Either the handler was removed (drop the spec entry) or it moved out of "
            f"{FIRMWARE_DIR} (teach this gate about the new location)."
        )

    undocumented_res = sorted(sets["responses"] - sets["spec_res"])
    if undocumented_res:
        problems.append(
            f"{len(undocumented_res)} firmware res:* reply/replies are missing from {SPEC}: {undocumented_res}. "
            f"Replies are built as `String(\"res:\") + type.substring(4)`, so documenting the request is not "
            f"enough — add the matching <Name>Response message and its channels['/ws'].subscribe $ref."
        )

    phantom_res = sorted(sets["spec_res"] - sets["responses"])
    if phantom_res:
        problems.append(
            f"{len(phantom_res)} res:* operation(s) in {SPEC} are never emitted by {FIRMWARE_DIR}/*.cpp: "
            f"{phantom_res}. Drop the spec entry, or point this gate at the code that emits it."
        )

    orphan_handlers = sorted(sets["handlers"] - sets["client"] - set(allow_clientless))
    if orphan_handlers:
        problems.append(
            f"{len(orphan_handlers)} firmware req:* handler(s) have no web-client caller: {orphan_handlers}. "
            f"If that is deliberate (legacy/HTTP-superseded), add it to the {_ALLOWLIST_BEGIN} block in "
            f"{CONTRACT_TEST} — that block is the single canonical allow-list."
        )

    unhandled_client = sorted(sets["client"] - sets["handlers"])
    if unhandled_client:
        problems.append(
            f"{len(unhandled_client)} req:* type(s) sent by the web client have no firmware handler: "
            f"{unhandled_client}. The device would silently ignore them."
        )

    stale_allow = sorted(set(allow_clientless) - sets["handlers"])
    if stale_allow:
        problems.append(
            f"the {_ALLOWLIST_BEGIN} allow-list lists req:* type(s) that no longer have a firmware handler: "
            f"{stale_allow}. Remove them from the block in {CONTRACT_TEST}."
        )

    return problems


def main():
    sets = collect()
    allow_clientless = clientless_handlers()
    problems = check(sets, allow_clientless)
    if problems:
        print("WebSocket API spec drift detected (PRO-610):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print(
        "WebSocket API spec is in sync: "
        f"{len(sets['handlers'])} req:* handlers, {len(sets['responses'])} res:* replies, "
        f"{len(sets['spec_req'])} documented requests, {len(sets['spec_res'])} documented responses, "
        f"{len(allow_clientless)} allow-listed client-less handlers."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
