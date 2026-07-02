#include "display/plugins/WsBroadcastClosePolicy.h"
#include <unity.h>

// PRO-357: WebUIPlugin's broadcastAll() sends WS frames (ws.textAll()) while
// holding the non-recursive `wsMutex` that PRO-313 introduced to serialize the
// client-list walk against the AsyncTCP task. The on-device coredump showed
// that enabling ESPAsyncWebServer's setCloseClientOnQueueFull(true) makes a
// full TX queue close the client INLINE inside textAll(), synchronously firing
// WS_EVT_DISCONNECT on the SAME task, which re-takes the non-recursive wsMutex
// -> self-deadlock -> AsyncTCP starvation -> Task Watchdog reboot.
//
// The deadlock itself is a runtime FreeRTOS/AsyncTCP condition and cannot be
// reproduced on host. What these tests pin is the lock-discipline DECISION the
// fix encodes (wsInlineCloseOnQueueFullIsSafe): the inline close-on-queue-full
// is only safe when the send is NOT performed under the serialization lock, or
// when that lock is recursive. WebUIPlugin's broadcast sends under a
// non-recursive wsMutex, so the inline close MUST stay disabled. A static_assert
// in WebUIPlugin.cpp's WS_EVT_CONNECT branch pins the production configuration;
// these tests pin the full truth table so a future tweak can't silently re-open
// PRO-357.

void setUp(void) {}
void tearDown(void) {}

// --- The WebUIPlugin configuration: sends under a non-recursive lock.

// This is the exact (sendsUnderSerializationLock=true, mutexIsRecursive=false)
// case that WebUIPlugin is in. The inline close is UNSAFE and must stay off.
// This pins the production decision so a regression flips this assertion.
//
// PRO-357 (review-anchored): this predicate only models the DECISION. The
// production safety net has two further parts that this test deliberately
// references so a future deletion is caught at the right layer:
//   1. WebUIPlugin's WS_EVT_CONNECT branch calls
//      client->setCloseClientOnQueueFull(false) to actually establish
//      closeWhenFull == false (the library default is TRUE, so the comment +
//      static_assert alone do NOT establish the safe state).
//   2. The same branch then asserts the runtime field matches this predicate
//      via client->willCloseClientOnQueueFull(), so deleting the setter trips a
//      runtime ESP_LOGE at the real call site (and, under display-sim, exercises
//      the same set-then-verify path the sim shim now models).
// Keep this predicate's result in lockstep with that runtime call: the value
// the setter establishes (false == "do not inline-close") is exactly the value
// this predicate returns for the WebUIPlugin configuration.
void test_webuiplugin_config_inline_close_unsafe(void) {
    TEST_ASSERT_FALSE(wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/true,
                                                     /*mutexIsRecursive=*/false));
}

// --- Safe configurations: when the deadlock precondition does not hold.

// If the send never happens while the serialization lock is held, the inline
// close cannot re-take a held lock, so it is safe regardless of recursion.
void test_no_lock_held_during_send_is_safe(void) {
    TEST_ASSERT_TRUE(wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/false,
                                                    /*mutexIsRecursive=*/false));
    TEST_ASSERT_TRUE(wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/false,
                                                    /*mutexIsRecursive=*/true));
}

// A recursive serialization mutex could tolerate the re-entrant re-take, so the
// inline close would not deadlock. (WebUIPlugin deliberately does NOT use a
// recursive mutex — switching to one to mask this would be the wrong fix — but
// the predicate must model the escape hatch correctly.)
void test_recursive_mutex_tolerates_reentrant_close(void) {
    TEST_ASSERT_TRUE(wsInlineCloseOnQueueFullIsSafe(/*sendsUnderSerializationLock=*/true,
                                                    /*mutexIsRecursive=*/true));
}

static int runWsBroadcastClosePolicyTests() {
    UNITY_BEGIN();
    RUN_TEST(test_webuiplugin_config_inline_close_unsafe);
    RUN_TEST(test_no_lock_held_during_send_is_safe);
    RUN_TEST(test_recursive_mutex_tolerates_reentrant_close);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() { runWsBroadcastClosePolicyTests(); }

void loop() {}
#else
int main(int argc, char **argv) { return runWsBroadcastClosePolicyTests(); }
#endif
