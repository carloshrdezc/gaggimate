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
  4. spec `components.messages` <->  the `channels['/ws']` message surface
     (a documented operation nobody `$ref`s from `publish`/`subscribe` is not
     actually part of the published API, and a `$ref` to a message that
     documents no operation — or documents one for the other direction — is a
     dangling entry)

Direction 3 has an ALLOW-LIST: three firmware handlers are deliberate
legacy/HTTP-superseded orphans with no web caller. It is NOT duplicated here —
it is parsed out of the marker block in
``web/src/services/ApiService.contract.test.js``, which already recorded those
three in its PRO-16 audit note, so there is exactly one list to maintain.

No third-party dependencies on purpose (no PyYAML): the spec is sliced into its
`components.messages`, `components.schemas` and `channels['/ws']` blocks by
indentation and then scanned with regexes, so this runs anywhere python3 does,
including as a cheap first step in CI.

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

# AsyncAPI operation markers: `enum: ['req:grinders:select']`. `evt:*` is matched
# too: an event message documents no req/res operation but is still a legitimate
# `subscribe` $ref target, so the channel cross-check must not call it dangling.
_SPEC_OPERATION_RE = re.compile(r"enum:\s*\['((?:req|res|evt):[^']+)'\]")

# A `components.messages.<Name>` / `components.schemas.<Name>` entry header.
_SPEC_COMPONENT_RE = re.compile(r"^    ([A-Za-z0-9_]+):[^\n]*$", re.M)

# Indirect payloads: `payload: {$ref: '#/components/schemas/OtaSettingsPayload'}`
# — six of the real messages keep their `tp` enum in the shared schema instead of
# inline, so the message -> operation map has to follow one hop.
_SPEC_SCHEMA_REF_RE = re.compile(r"\$ref:\s*'#/components/schemas/([A-Za-z0-9_]+)'")

# A channel message reference: `- $ref: '#/components/messages/AuthRequest'`.
_SPEC_MESSAGE_REF_RE = re.compile(r"\$ref:\s*'#/components/messages/([A-Za-z0-9_]+)'")

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


def _yaml_block(text, key, indent=""):
    """The lines nested under `<indent><key>:`, exclusive of the key line itself.

    Ends at the first non-blank line indented at or below `indent`. The spec is
    machine-uniform 2-space YAML with no block scalars at these levels, so
    indentation slicing is enough — and it keeps the no-PyYAML constraint.
    """
    match = re.search(rf"^{re.escape(indent)}{re.escape(key)}:[^\n]*\n", text, re.M)
    if match is None:
        return ""
    kept = []
    for line in text[match.end() :].splitlines(keepends=True):
        if line.strip() and not line.startswith(indent + " "):
            break
        kept.append(line)
    return "".join(kept)


def _components(block):
    """Split a `components.*` block into {EntryName: entry text}."""
    marks = [(m.group(1), m.start()) for m in _SPEC_COMPONENT_RE.finditer(block)]
    bounds = [start for _name, start in marks] + [len(block)]
    return {name: block[start:end] for (name, start), end in zip(marks, bounds[1:])}


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


def spec_message_operations(spec_text):
    """Map every `components.messages.<Name>` to the `tp` it documents.

    The value is the `req:*`/`res:*`/`evt:*` operation the message pins, or None
    when the message documents no `tp` at all (a `$ref` target like that is
    dangling, not an operation).
    """
    components = _yaml_block(spec_text, "components")
    schema_ops = {}
    for name, body in _components(_yaml_block(components, "schemas", "  ")).items():
        found = _SPEC_OPERATION_RE.search(body)
        schema_ops[name] = found.group(1) if found else None
    operations = {}
    for name, body in _components(_yaml_block(components, "messages", "  ")).items():
        inline = _SPEC_OPERATION_RE.search(body)
        if inline:
            operations[name] = inline.group(1)
            continue
        # Indirect: the tp enum lives in the schema the payload $refs.
        operations[name] = next(
            (schema_ops[ref] for ref in _SPEC_SCHEMA_REF_RE.findall(body) if schema_ops.get(ref)),
            None,
        )
    return operations


def spec_channel_refs(spec_text):
    """The messages `$ref`d under `channels['/ws']`, as (publish, subscribe)."""
    ws = _yaml_block(_yaml_block(spec_text, "channels"), "'/ws'", "  ")
    return (
        set(_SPEC_MESSAGE_REF_RE.findall(_yaml_block(ws, "publish", "    "))),
        set(_SPEC_MESSAGE_REF_RE.findall(_yaml_block(ws, "subscribe", "    "))),
    )


def spec_sets(spec_text):
    """The spec side of the gate: documented operations AND their channel wiring.

    A message only counts as documenting an operation when BOTH halves line up:
    the component pins the `tp` enum *and* the component is `$ref`d from the
    matching channel section (`publish` for `req:*`, `subscribe` for `res:*`).
    Reporting the two halves separately is what makes a deleted `$ref` fail here
    instead of sliding through as a still-present component enum.
    """
    operations = spec_message_operations(spec_text)
    publish, subscribe = spec_channel_refs(spec_text)
    wired = {"req:": publish, "res:": subscribe, "evt:": subscribe}
    return {
        "spec_req": {op for op in operations.values() if op and op.startswith("req:")},
        "spec_res": {op for op in operations.values() if op and op.startswith("res:")},
        # Component pins the operation, but no channel $ref exposes it.
        "spec_unwired": sorted(
            f"{op} ({name})" for name, op in operations.items() if op and name not in wired[op[:4]]
        ),
        # Channel $ref with nothing (or the wrong direction) behind it.
        "spec_dangling_publish": sorted(
            f"{name} -> {operations.get(name) or 'no tp enum'}"
            for name in publish
            if not (operations.get(name) or "").startswith("req:")
        ),
        "spec_dangling_subscribe": sorted(
            f"{name} -> {operations.get(name) or 'no tp enum'}"
            for name in subscribe
            if not (operations.get(name) or "").startswith(("res:", "evt:"))
        ),
    }


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
    client = client_request_types(_sources(WEB_SRC, (".js", ".jsx"), skip_tests=True))
    return {
        "handlers": handlers,
        "responses": responses,
        "client": client,
        **spec_sets(_read(SPEC)),
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

    if sets["spec_unwired"]:
        problems.append(
            f"{len(sets['spec_unwired'])} operation(s) documented as a components.messages entry in {SPEC} "
            f"are not exposed on the channel: {sets['spec_unwired']}. Add the missing "
            f"$ref: '#/components/messages/<Name>' under channels['/ws'].publish (req:*) or "
            f".subscribe (res:*) — a component nobody references documents nothing."
        )

    for section, key in (("publish", "spec_dangling_publish"), ("subscribe", "spec_dangling_subscribe")):
        if sets[key]:
            expected = "req:*" if section == "publish" else "res:*/evt:*"
            problems.append(
                f"{len(sets[key])} channels['/ws'].{section} $ref(s) in {SPEC} do not resolve to a "
                f"{expected} components.messages entry: {sets[key]}. Fix the $ref name, or give the "
                f"message a tp enum in the right direction."
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
