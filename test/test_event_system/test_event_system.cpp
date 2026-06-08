#include <display/core/Event.h>
#include <display/core/PluginManager.h>
#include <unity.h>

// Exercises the core plugin/event system that the firmware is built around:
// string-based event IDs, typed EventDataEntry (int/float/string), listener
// dispatch, and the stopPropagation early-out. PluginManager.cpp is compiled
// into this host program by [env:native] build_src_filter; the only hardware
// dependency it has is Arduino's String, satisfied by test/native/Arduino.h.

void setUp(void) {}
void tearDown(void) {}

void test_typed_event_data_round_trips(void) {
    Event event;
    event.id = "test:typed";
    event.setInt("count", 7);
    event.setFloat("ratio", 1.5f);
    event.setString("name", "lever");

    TEST_ASSERT_EQUAL_INT(7, event.getInt("count"));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, event.getFloat("ratio"));
    TEST_ASSERT_EQUAL_STRING("lever", event.getString("name").c_str());

    // Typed accessors must not cross types: asking for the wrong type, or a
    // missing key, yields the zero/empty default rather than a bogus value.
    TEST_ASSERT_EQUAL_INT(0, event.getInt("ratio"));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, event.getFloat("count"));
    TEST_ASSERT_EQUAL_STRING("", event.getString("missing").c_str());
}

void test_listener_receives_triggered_event_by_string_id(void) {
    PluginManager manager;
    int seen = 0;
    String seenValue;
    manager.on("brew:start", [&](Event &e) {
        seen++;
        seenValue = e.getString("profile");
    });

    Event result = manager.trigger("brew:start", "profile", String("morning"));

    TEST_ASSERT_EQUAL_INT(1, seen);
    TEST_ASSERT_EQUAL_STRING("morning", seenValue.c_str());
    TEST_ASSERT_EQUAL_STRING("morning", result.getString("profile").c_str());
}

void test_event_ids_are_isolated(void) {
    PluginManager manager;
    int a = 0, b = 0;
    manager.on("evt:a", [&](Event &) { a++; });
    manager.on("evt:b", [&](Event &) { b++; });

    manager.trigger("evt:a");

    TEST_ASSERT_EQUAL_INT(1, a);
    TEST_ASSERT_EQUAL_INT(0, b);
}

void test_multiple_listeners_run_in_registration_order(void) {
    PluginManager manager;
    std::vector<int> order;
    manager.on("evt:multi", [&](Event &) { order.push_back(1); });
    manager.on("evt:multi", [&](Event &) { order.push_back(2); });

    manager.trigger("evt:multi");

    TEST_ASSERT_EQUAL_UINT(2, order.size());
    TEST_ASSERT_EQUAL_INT(1, order[0]);
    TEST_ASSERT_EQUAL_INT(2, order[1]);
}

void test_stop_propagation_halts_remaining_listeners(void) {
    PluginManager manager;
    int first = 0, second = 0;
    manager.on("evt:stop", [&](Event &e) {
        first++;
        e.stopPropagation = true;
    });
    manager.on("evt:stop", [&](Event &) { second++; });

    manager.trigger("evt:stop");

    TEST_ASSERT_EQUAL_INT(1, first);
    TEST_ASSERT_EQUAL_INT(0, second); // never reached after stopPropagation
}

void test_trigger_unregistered_event_is_noop(void) {
    PluginManager manager;
    // No listeners registered for this id; triggering must not crash and the
    // returned event simply carries the id and supplied data back.
    Event result = manager.trigger("evt:nobody", "k", 42);
    TEST_ASSERT_EQUAL_STRING("evt:nobody", result.id.c_str());
    TEST_ASSERT_EQUAL_INT(42, result.getInt("k"));
}

static int runEventSystemTests() {
    UNITY_BEGIN();
    RUN_TEST(test_typed_event_data_round_trips);
    RUN_TEST(test_listener_receives_triggered_event_by_string_id);
    RUN_TEST(test_event_ids_are_isolated);
    RUN_TEST(test_multiple_listeners_run_in_registration_order);
    RUN_TEST(test_stop_propagation_halts_remaining_listeners);
    RUN_TEST(test_trigger_unregistered_event_is_noop);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runEventSystemTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runEventSystemTests(); }
#endif
