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
    SPEC,
    _function_blocks,
    check,
    client_request_types,
    clientless_handlers,
    collect,
    derived_response_types,
    handler_types,
    spec_channel_refs,
    spec_message_operations,
    spec_sets,
)

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _read_spec():
    with open(os.path.join(PROJECT_ROOT, *SPEC.split("/")), encoding="utf-8") as fh:
        return fh.read()


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

# The matching spec: every operation above documented as a components.messages
# entry AND exposed by a channels['/ws'] $ref in the right direction. Both halves
# matter — see ChannelRefs below.
SPEC_IN_SYNC = """
channels:
  '/ws':
    description: All messages share the same WebSocket channel.
    subscribe:
      message:
        oneOf:
          - $ref: '#/components/messages/StatusEvent'
          - $ref: '#/components/messages/GrindersListResponse'
          - $ref: '#/components/messages/GrindersSelectResponse'
          - $ref: '#/components/messages/FlushStartResponse'
    publish:
      message:
        oneOf:
          - $ref: '#/components/messages/BeansSelectRequest'
          - $ref: '#/components/messages/GrindersListRequest'
          - $ref: '#/components/messages/GrindersSelectRequest'
components:
  schemas:
    StatusPayload:
      type: object
      properties:
        tp:
          type: string
          enum: ['evt:status']
  messages:
    StatusEvent:
      payload:
        $ref: '#/components/schemas/StatusPayload'
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
    return {
        "handlers": handler_types(firmware),
        "responses": derived_response_types(firmware),
        "client": client_request_types([("client.jsx", client)]),
        **spec_sets(spec),
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

    def test_spec_sets_splits_requests_and_responses(self):
        sets = spec_sets(SPEC_IN_SYNC)
        self.assertIn("req:grinders:select", sets["spec_req"])
        self.assertIn("res:grinders:select", sets["spec_res"])
        self.assertTrue(all(op.startswith("req:") for op in sets["spec_req"]))
        self.assertTrue(all(op.startswith("res:") for op in sets["spec_res"]))
        # evt:* is parsed (so it is not a dangling subscribe $ref) but is not a
        # req/res operation, so it must stay out of both firmware-compared sets.
        self.assertNotIn("evt:status", sets["spec_req"] | sets["spec_res"])

    def test_spec_message_operations_maps_names_to_tp_including_schema_payloads(self):
        operations = spec_message_operations(SPEC_IN_SYNC)
        self.assertEqual(operations["GrindersSelectRequest"], "req:grinders:select")
        self.assertEqual(operations["GrindersSelectResponse"], "res:grinders:select")
        # StatusEvent keeps its tp enum one hop away, in the schema its payload
        # $refs — six real messages are shaped like that, and a mapper that only
        # looked inline would call every one of them a dangling subscribe $ref.
        self.assertEqual(operations["StatusEvent"], "evt:status")

    def test_spec_channel_refs_separates_publish_from_subscribe(self):
        publish, subscribe = spec_channel_refs(SPEC_IN_SYNC)
        self.assertIn("GrindersSelectRequest", publish)
        self.assertNotIn("GrindersSelectRequest", subscribe)
        self.assertIn("GrindersSelectResponse", subscribe)
        self.assertNotIn("GrindersSelectResponse", publish)

    def test_client_request_types_drops_the_bare_prefix_literal(self):
        # ApiService.js contains `data.tp.startsWith('req:')`, which is not a
        # message type.
        self.assertEqual(client_request_types([("a.js", "x.startsWith('req:')")]), set())


class FunctionBoundaries(unittest.TestCase):
    """Attribution must work for every function-definition shape, not just members.

    The first cut of the block splitter only recognised `Class::method(`
    definitions, so a handler living in a free (or `static` free) function — or
    in anything defined before the first member definition in the file — was
    folded into a neighbouring block. When such a function held the dynamic
    `String("res:") + type.substring(4)` reply, `derived_response_types()`
    returned an EMPTY set for it and the gate missed the undocumented `res:*`
    reply entirely. These are the regressions for that.
    """

    # The reviewer's verbatim repro: a free function, no `::` anywhere, no
    # member definition in the file at all. Used to yield set().
    FREE_FUNCTION = """
void helper() {
    String type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    if (type == "req:free:op") {
        // ...
    }
}
"""

    def test_free_function_with_the_dynamic_idiom_is_attributed(self):
        self.assertEqual(derived_response_types(self.FREE_FUNCTION), {"res:free:op"})

    def test_static_free_function_with_the_dynamic_idiom_is_attributed(self):
        # WebUIPlugin.cpp really defines `static String relayTokenProtocol(...)`
        # and friends at file scope; the shape has to be a boundary too.
        src = self.FREE_FUNCTION.replace("void helper()", "static void helper()")
        self.assertEqual(derived_response_types(src), {"res:free:op"})

    def test_free_function_before_the_first_member_definition_is_attributed(self):
        # The precise failure mode: the free handler precedes every
        # `Class::method(` in the translation unit, so a member-only boundary
        # regex started its first block AFTER it and dropped it on the floor.
        src = self.FREE_FUNCTION + FIRMWARE
        derived = derived_response_types(src)
        self.assertIn("res:free:op", derived)
        # ...and the member functions keep their own attribution.
        self.assertEqual(
            derived,
            {"res:free:op", "res:grinders:list", "res:grinders:select", "res:flush:start"},
        )

    def test_free_function_without_the_idiom_does_not_leak_replies(self):
        # The boundary must isolate in both directions: a free function whose
        # branches answer literally must not inherit a neighbour's dynamic idiom.
        src = (
            'void plain(int x) {\n    if (type == "req:plain:op") {\n        // ...\n    }\n}\n'
            + self.FREE_FUNCTION
        )
        self.assertEqual(derived_response_types(src), {"res:free:op"})

    def test_forward_declarations_and_calls_are_not_boundaries(self):
        # A declaration ends in `;` (WebUIPlugin.cpp really forward-declares
        # `static String resolveReleaseUrl(const String &channel);`) and an
        # indented call/lambda is inside a body — treating either as a boundary
        # would cut a handler away from its dynamic reply line.
        src = (
            "static String resolveReleaseUrl(const String &channel);\n"
            "void WebUIPlugin::handleThing(uint32_t clientId, JsonDocument &request) {\n"
            '    auto type = request["tp"].as<String>();\n'
            '    response["tp"] = String("res:") + type.substring(4);\n'
            "    server.on(\"/x\", HTTP_GET, [this](AsyncWebServerRequest *request) {\n"
            "        serveWebAsset(request);\n"
            "    });\n"
            '    if (type == "req:thing:go") {\n'
            "        // ...\n"
            "    }\n"
            "}\n"
        )
        self.assertEqual(derived_response_types(src), {"res:thing:go"})
        # One function in, one block out — the lambda and the declaration must
        # not have produced extra blocks.
        self.assertEqual(len(list(_function_blocks(src))), 1)

    def test_multiline_parameter_lists_stay_one_block(self):
        # ShotHistoryPlugin::handleCompletedShot really wraps its parameter list;
        # the `{`-vs-`;` test has to balance parens across the newline.
        src = (
            "void Plugin::handleWrapped(uint32_t clientId, JsonDocument &request,\n"
            "                           const String &beanName) {\n"
            '    response["tp"] = String("res:") + type.substring(4);\n'
            '    if (type == "req:wrapped:op") {\n'
            "    }\n"
            "}\n"
        )
        self.assertEqual(derived_response_types(src), {"res:wrapped:op"})
        self.assertEqual(len(list(_function_blocks(src))), 1)


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

    def test_undocumented_reply_from_a_free_function_handler_fails_the_gate(self):
        # End-to-end form of the boundary bug: the new handler is a FREE function
        # (the shape the old member-only regex could not see) and neither its
        # request nor its dynamically-derived reply is documented. Both must be
        # named. Before the fix the res:* half of this was invisible.
        firmware = FunctionBoundaries.FREE_FUNCTION + FIRMWARE
        client = CLIENT + "api.send({ tp: 'req:free:op' });\n"
        joined = " ".join(check(sets_from(firmware=firmware, client=client), allow_clientless=frozenset()))
        self.assertIn("req:free:op", joined)
        self.assertIn("res:free:op", joined)


class ChannelRefs(unittest.TestCase):
    """A component enum alone is not documentation — the channel must expose it.

    The first cut of this gate scanned for `enum: ['req:...']` anywhere in the
    file, so deleting the `channels['/ws']` $refs added for req/res
    `grinders:select` left it green while the AsyncAPI-published message surface
    no longer mentioned the operation at all. These are the regressions for that.
    """

    @staticmethod
    def _joined(spec):
        return " ".join(check(sets_from(spec=spec), allow_clientless=frozenset()))

    def test_request_component_without_a_publish_ref_fails_the_gate(self):
        spec = SPEC_IN_SYNC.replace("          - $ref: '#/components/messages/GrindersSelectRequest'\n", "")
        joined = self._joined(spec)
        self.assertIn("req:grinders:select (GrindersSelectRequest)", joined)
        self.assertIn("not exposed on the channel", joined)

    def test_response_component_without_a_subscribe_ref_fails_the_gate(self):
        spec = SPEC_IN_SYNC.replace("          - $ref: '#/components/messages/GrindersSelectResponse'\n", "")
        joined = self._joined(spec)
        self.assertIn("res:grinders:select (GrindersSelectResponse)", joined)
        self.assertIn("not exposed on the channel", joined)

    def test_dropping_both_refs_flags_both_directions(self):
        # The exact PRO-610 bounce scenario: the two new $refs are deleted, the
        # component enums stay. Must fail, naming both.
        spec = SPEC_IN_SYNC.replace(
            "          - $ref: '#/components/messages/GrindersSelectRequest'\n", ""
        ).replace("          - $ref: '#/components/messages/GrindersSelectResponse'\n", "")
        joined = self._joined(spec)
        self.assertIn("req:grinders:select (GrindersSelectRequest)", joined)
        self.assertIn("res:grinders:select (GrindersSelectResponse)", joined)

    def test_publish_ref_to_a_nonexistent_component_fails_the_gate(self):
        spec = SPEC_IN_SYNC.replace(
            "          - $ref: '#/components/messages/GrindersSelectRequest'",
            "          - $ref: '#/components/messages/GrindersSelectRequest'\n"
            "          - $ref: '#/components/messages/GrindersTypoRequest'",
        )
        joined = self._joined(spec)
        self.assertIn("GrindersTypoRequest -> no tp enum", joined)
        self.assertIn("channels['/ws'].publish $ref(s)", joined)

    def test_subscribe_ref_to_a_component_without_a_tp_enum_fails_the_gate(self):
        flush_with_tp = (
            "    FlushStartResponse:\n      payload:\n        properties:\n"
            "          tp:\n            enum: ['res:flush:start']\n"
        )
        flush_without_tp = "    FlushStartResponse:\n      payload:\n        properties:\n          ok:\n            type: boolean\n"
        spec = SPEC_IN_SYNC.replace(flush_with_tp, flush_without_tp)
        joined = self._joined(spec)
        self.assertIn("FlushStartResponse -> no tp enum", joined)
        self.assertIn("channels['/ws'].subscribe $ref(s)", joined)

    def test_ref_under_the_wrong_channel_section_fails_the_gate(self):
        # Moving the request $ref into `subscribe` satisfies "a $ref exists
        # somewhere" but documents the wrong direction: publish is client->device.
        spec = SPEC_IN_SYNC.replace(
            "          - $ref: '#/components/messages/GrindersSelectRequest'\n", ""
        ).replace(
            "          - $ref: '#/components/messages/GrindersSelectResponse'",
            "          - $ref: '#/components/messages/GrindersSelectResponse'\n"
            "          - $ref: '#/components/messages/GrindersSelectRequest'",
        )
        joined = self._joined(spec)
        self.assertIn("req:grinders:select (GrindersSelectRequest)", joined)
        self.assertIn("GrindersSelectRequest -> req:grinders:select", joined)

    def test_event_messages_are_legitimate_subscribe_refs(self):
        # evt:* messages have no req/res counterpart in the firmware sets, so a
        # correctly-wired one must be neither "unwired" nor a dangling $ref.
        sets = sets_from()
        self.assertEqual(sets["spec_dangling_subscribe"], [])
        self.assertEqual([p for p in sets["spec_unwired"] if "Status" in p], [])

    def test_event_component_without_a_subscribe_ref_fails_the_gate(self):
        # Events are server->client too: dropping the $ref un-publishes evt:status
        # from the channel just as surely as it would a res:*.
        spec = SPEC_IN_SYNC.replace("          - $ref: '#/components/messages/StatusEvent'\n", "")
        joined = self._joined(spec)
        self.assertIn("evt:status (StatusEvent)", joined)
        self.assertIn("not exposed on the channel", joined)


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

    def test_real_spec_channel_refs_cover_every_documented_operation(self):
        # The reviewer's finding, asserted against real data: every req:*/res:*
        # component in docs/websocket-api.yaml is $ref'd from the matching
        # channel section, and every channel $ref resolves to one.
        self.assertEqual(self.sets["spec_unwired"], [])
        self.assertEqual(self.sets["spec_dangling_publish"], [])
        self.assertEqual(self.sets["spec_dangling_subscribe"], [])

    def test_real_spec_channel_ref_lists_are_not_vacuously_empty(self):
        # Guards the guard: a slicing bug that returned no $refs at all would
        # make the cross-check above pass while checking nothing.
        publish, subscribe = spec_channel_refs(_read_spec())
        self.assertGreater(len(publish), 40)
        self.assertGreater(len(subscribe), 20)
        self.assertIn("GrindersSelectRequest", publish)
        self.assertIn("GrindersSelectResponse", subscribe)

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
        self.assertEqual(allow, frozenset({"req:beans:load", "req:history:list", "req:history:get", "req:brew-temperature:set"}))
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
