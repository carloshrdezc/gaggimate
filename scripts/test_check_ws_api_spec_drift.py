#!/usr/bin/env python3
"""Regression tests for scripts/check_ws_api_spec_drift.py (PRO-610).

Two layers, mirroring the shape of scripts/test_select_tidy_sources.py:

  1. Unit tests over SYNTHETIC firmware/spec/client sources — they pin the
     parsing and the set-difference contract, including a synthetic replay of the
     exact PRO-610 defect (an undocumented `req:grinders:select` handler whose
     dynamic `res:grinders:select` reply was undocumented too). This is the
     permanent form of the "temporarily add a dummy handler and confirm the gate
     fails" verification: a stable string-diff fixture instead of a real dummy
     handler nobody would remember to delete.
  2. Tests over the REAL working tree — the spec must actually be in sync now,
     and the allow-list here must stay identical to the one recorded in
     web/src/services/ApiService.contract.test.js.

Run with no dependencies (there is no pytest in this repo):

    python3 scripts/test_check_ws_api_spec_drift.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from check_ws_api_spec_drift import (  # noqa: E402
    CONTRACT_TEST,
    check,
    client_request_types,
    clientless_handlers,
    collect,
    derived_response_types,
    handler_types,
    spec_operations,
)

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A miniature WebUIPlugin.cpp: one dispatch router plus a sub-handler that builds
# its reply tp dynamically, which is the idiom that hid res:grinders:select.
FIRMWARE = """
void WebUIPlugin::processWebSocketMessage(uint32_t clientId, JsonDocument &request) {
    auto msgType = request["tp"].as<String>();
    if (msgType.startsWith("req:grinders:")) {
        handleGrinderRequest(clientId, request);
    } else if (msgType == "req:beans:select") {
        controller->getSettings().setSelectedBean(request["name"].as<String>());
    }
}

void WebUIPlugin::handleGrinderRequest(uint32_t clientId, JsonDocument &request) {
    auto type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    if (type == "req:grinders:list") {
        // ...
    } else if (type == "req:grinders:select") {
        // ...
    }
    sendResponse(clientId, response);
}

void WebUIPlugin::sendFlushResponse(uint32_t clientId) {
    response["tp"] = "res:flush:start";
}
"""

# The matching spec: every operation above documented.
SPEC_IN_SYNC = """
channels:
  '/ws':
    subscribe:
      message:
        oneOf:
          - $ref: '#/components/messages/GrindersSelectResponse'
components:
  messages:
    BeansSelectRequest:
      payload:
        properties:
          tp:
            enum: ['req:beans:select']
    GrindersListRequest:
      payload:
        properties:
          tp:
            enum: ['req:grinders:list']
    GrindersSelectRequest:
      payload:
        properties:
          tp:
            enum: ['req:grinders:select']
    GrindersListResponse:
      payload:
        properties:
          tp:
            enum: ['res:grinders:list']
    GrindersSelectResponse:
      payload:
        properties:
          tp:
            enum: ['res:grinders:select']
    FlushStartResponse:
      payload:
        properties:
          tp:
            enum: ['res:flush:start']
"""

CLIENT = """
api.send({ tp: 'req:grinders:select', name: opt.name });
api.request({ tp: 'req:grinders:list' });
api.send({ tp: 'req:beans:select', name: bean.name });
"""


def sets_from(firmware=FIRMWARE, spec=SPEC_IN_SYNC, client=CLIENT):
    spec_req, spec_res = spec_operations(spec)
    return {
        "handlers": handler_types(firmware),
        "responses": derived_response_types(firmware),
        "spec_req": spec_req,
        "spec_res": spec_res,
        "client": client_request_types([("client.jsx", client)]),
    }


class Parsing(unittest.TestCase):
    def test_handler_types_reads_equality_branches_only(self):
        # `.startsWith("req:grinders:")` is a router, not a handler declaration.
        self.assertEqual(
            handler_types(FIRMWARE),
            {"req:beans:select", "req:grinders:list", "req:grinders:select"},
        )

    def test_negative_comparisons_are_not_handlers(self):
        # WebUIPlugin.cpp really contains `msgType != "req:beans:select"` as part
        # of a router condition; it must not be mistaken for a second handler,
        # and it must not be the ONLY evidence a handler exists.
        src = 'if (msgType.startsWith("req:beans:") && msgType != "req:beans:select") { handleBeanRequest(); }'
        self.assertEqual(handler_types(src), set())

    def test_derived_response_types_expands_the_dynamic_reply_idiom(self):
        # THE PRO-610 mechanism: res:grinders:select is never written as a
        # string literal anywhere in the firmware — it only exists because the
        # reply tp is derived from the request tp.
        self.assertNotIn('"res:grinders:select"', FIRMWARE)
        self.assertEqual(
            derived_response_types(FIRMWARE),
            {"res:grinders:list", "res:grinders:select", "res:flush:start"},
        )

    def test_derived_response_types_ignores_handlers_in_functions_without_the_idiom(self):
        # req:beans:select is dispatched inline in processWebSocketMessage, which
        # has no dynamic reply tp, so it must NOT imply a res:beans:select.
        self.assertNotIn("res:beans:select", derived_response_types(FIRMWARE))

    def test_spec_operations_splits_requests_and_responses(self):
        spec_req, spec_res = spec_operations(SPEC_IN_SYNC)
        self.assertIn("req:grinders:select", spec_req)
        self.assertIn("res:grinders:select", spec_res)
        self.assertTrue(all(op.startswith("req:") for op in spec_req))
        self.assertTrue(all(op.startswith("res:") for op in spec_res))

    def test_client_request_types_drops_the_bare_prefix_literal(self):
        # ApiService.js contains `data.tp.startsWith('req:')`, which is not a
        # message type.
        self.assertEqual(client_request_types([("a.js", "x.startsWith('req:')")]), set())


class DriftDetection(unittest.TestCase):
    def test_synthetic_in_sync_fixture_is_green(self):
        # Guards the guard: if this fixture ever failed, the negative cases below
        # would be passing for the wrong reason.
        self.assertEqual(check(sets_from(), allow_clientless=frozenset()), [])

    def test_undocumented_handler_fails_the_gate(self):
        # A verbatim replay of PRO-610: drop the GrindersSelectRequest entry (and
        # its response) from the spec and the gate must fail, naming both.
        spec = SPEC_IN_SYNC.replace("enum: ['req:grinders:select']", "enum: ['req:grinders:nope']").replace(
            "enum: ['res:grinders:select']", "enum: ['res:grinders:nope']"
        )
        problems = check(sets_from(spec=spec), allow_clientless=frozenset())
        joined = " ".join(problems)
        self.assertIn("req:grinders:select", joined)
        self.assertIn("res:grinders:select", joined)
        self.assertIn("missing from docs/websocket-api.yaml", joined)

    def test_a_brand_new_undocumented_handler_fails_the_gate(self):
        # The forward-looking case: someone adds a handler and forgets the spec.
        firmware = FIRMWARE.replace(
            '    } else if (type == "req:grinders:select") {',
            '    } else if (type == "req:grinders:dummy") {\n        // ...\n    } else if (type == "req:grinders:select") {',
        )
        problems = check(sets_from(firmware=firmware), allow_clientless=frozenset())
        joined = " ".join(problems)
        self.assertIn("req:grinders:dummy", joined)
        self.assertIn("res:grinders:dummy", joined)

    def test_documented_operation_with_no_handler_fails_the_gate(self):
        # The other direction: a handler is deleted but the spec entry lingers.
        firmware = FIRMWARE.replace('    } else if (type == "req:grinders:select") {\n        // ...\n', "")
        problems = check(sets_from(firmware=firmware), allow_clientless=frozenset())
        joined = " ".join(problems)
        self.assertIn("no firmware handler", joined)
        self.assertIn("req:grinders:select", joined)

    def test_client_calling_an_unhandled_type_fails_the_gate(self):
        client = CLIENT + "api.send({ tp: 'req:grinders:teleport' });\n"
        problems = check(sets_from(client=client), allow_clientless=frozenset())
        self.assertIn("req:grinders:teleport", " ".join(problems))

    def test_handler_with_no_client_caller_fails_unless_allow_listed(self):
        client = CLIENT.replace("api.send({ tp: 'req:beans:select', name: bean.name });", "")
        problems = check(sets_from(client=client), allow_clientless=frozenset())
        self.assertIn("no web-client caller", " ".join(problems))
        # Allow-listing it is the documented escape hatch.
        self.assertEqual(check(sets_from(client=client), allow_clientless=frozenset({"req:beans:select"})), [])

    def test_stale_allow_list_entry_fails_the_gate(self):
        # An allow-listed type whose handler is gone means the allow-list is
        # hiding nothing and should shrink.
        problems = check(sets_from(), allow_clientless=frozenset({"req:history:retired"}))
        self.assertIn("req:history:retired", " ".join(problems))


class RealTree(unittest.TestCase):
    """The gate must be green on the working tree, on the real sets."""

    def setUp(self):
        self.sets = collect()

    def test_no_drift_in_the_repository(self):
        self.assertEqual(check(self.sets, clientless_handlers()), [])

    def test_grinders_select_is_documented(self):
        # PRO-610's actual acceptance criterion, pinned so a spec cleanup that
        # drops the entry fails here with a name rather than as a set diff.
        self.assertIn("req:grinders:select", self.sets["spec_req"])
        self.assertIn("res:grinders:select", self.sets["spec_res"])
        self.assertIn("req:grinders:select", self.sets["handlers"])
        self.assertIn("res:grinders:select", self.sets["responses"])

    def test_inventories_are_not_vacuously_empty(self):
        # Guards the guard: a broken walk root or regex would make every
        # set-difference assertion pass with nothing in it.
        self.assertGreater(len(self.sets["handlers"]), 40)
        self.assertGreater(len(self.sets["responses"]), 20)
        self.assertGreater(len(self.sets["client"]), 40)

    def test_allow_list_comes_from_the_web_contract_test(self):
        # PRO-610 acceptance criterion: ONE allow-list of intentional
        # asymmetries. The gate reads it from the marker block in
        # web/src/services/ApiService.contract.test.js rather than keeping a
        # second copy; assert that (a) it parses to the three recorded orphans and
        # (b) those are exactly the handlers with no web caller today, so the
        # block cannot silently over- or under-cover.
        allow = clientless_handlers()
        self.assertEqual(allow, frozenset({"req:beans:load", "req:history:list", "req:history:get"}))
        self.assertEqual(self.sets["handlers"] - self.sets["client"], set(allow))

    def test_missing_allow_list_block_is_a_loud_error(self):
        # A silently-empty allow-list would fail CI for every intentional orphan;
        # a silently-ignored one would let real orphans through. Both must raise.
        with self.assertRaises(SystemExit):
            clientless_handlers("// no markers here\n")
        with self.assertRaises(SystemExit):
            clientless_handlers("// PRO-610-ALLOWLIST-BEGIN\n// (nothing)\n// PRO-610-ALLOWLIST-END\n")

    def test_allow_list_block_is_present_in_the_contract_test_file(self):
        # Names the file in the failure message if someone tidies the comment away.
        path = os.path.join(PROJECT_ROOT, *CONTRACT_TEST.split("/"))
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        self.assertIn("PRO-610-ALLOWLIST-BEGIN", text)
        self.assertIn("PRO-610-ALLOWLIST-END", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
