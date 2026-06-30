#ifndef WSBROADCASTCLOSEPOLICY_H
#define WSBROADCASTCLOSEPOLICY_H

// PRO-357: lock-discipline policy for WebUIPlugin's WebSocket broadcast path.
//
// THE BUG (verified from an on-device coredump): the Arduino loop task
// self-deadlocked on the NON-RECURSIVE FreeRTOS mutex `wsMutex`.
//   1. broadcastAll() takes wsMutex, then calls ws.textAll(msg).
//   2. ESPAsyncWebServer (v3.9.1) walks the client list inside textAll() and
//      enqueues to each client. When a client's TX queue is FULL *and*
//      setCloseClientOnQueueFull(true) was set at WS_EVT_CONNECT, the library
//      calls client->close() INLINE, synchronously, on the SAME loop task,
//      still inside textAll() — i.e. while wsMutex is HELD.
//   3. close() drives ~AsyncWebSocketClient() -> the ws.onEvent WS_EVT_DISCONNECT
//      branch INLINE on the same task, which re-takes the same non-recursive
//      wsMutex -> SELF-DEADLOCK.
//   4. The AsyncTCP task then blocks forever on wsMutex too, stops petting the
//      Task Watchdog, and the TWDT reboots the board
//      ("Task watchdog got triggered ... async_tcp").
//
// THE FIX: do NOT enable the library's inline close-on-queue-full on a client
// whose frames are broadcast while wsMutex is held. With the inline close
// disabled, a full TX queue DROPS the new frame (the queue is hard-capped at
// WS_MAX_QUEUED_MESSAGES, so it cannot grow without bound) instead of
// synchronously re-entering the disconnect callback. That removes the
// re-entrancy, so the textAll() send can safely stay inside the wsMutex
// critical section that PRO-313 requires for the client-list walk.
//
// The deadlock itself is a runtime FreeRTOS/AsyncTCP condition and cannot be
// reproduced in a host unit test. What CAN be pinned on host is the policy
// decision the fix encodes: given that a WS client's outgoing frames are sent
// while a serialization lock is held, is it safe to enable the library's
// inline close-on-queue-full for that client? This header captures that as a
// PURE decision with no Arduino / network / FreeRTOS dependencies, so it links
// and runs in [env:native] (mirrors WsReassemblyPolicy.h / SdReadRetryPolicy.h /
// ChangeModeDeferPolicy.h). The test pins it so a future change can't silently
// re-enable the inline close on the locked broadcast path and re-open PRO-357.

// Returns true  => it is SAFE to enable setCloseClientOnQueueFull(true).
// Returns false => the inline close MUST stay DISABLED, because a full-queue
//                  close would fire WS_EVT_DISCONNECT synchronously on the
//                  sending task and re-take the (non-recursive) serialization
//                  mutex that the send is performed under -> self-deadlock.
//
// `sendsUnderSerializationLock` is true when the client's frames are ever
// transmitted while the wsMutex serialization lock is held (which is exactly
// the case for broadcastAll() -> ws.textAll(), the steady-state heartbeat
// path). `mutexIsRecursive` is the escape hatch: a recursive mutex could
// tolerate the re-entrant re-take, but wsMutex is intentionally non-recursive
// (a recursive mutex would mask the design problem; see PRO-357 discussion),
// so this is false for WebUIPlugin and the inline close must stay off.
constexpr bool wsInlineCloseOnQueueFullIsSafe(bool sendsUnderSerializationLock, bool mutexIsRecursive) {
    // Safe only if the send never happens under the lock, or the lock can be
    // re-taken re-entrantly without deadlocking.
    return !sendsUnderSerializationLock || mutexIsRecursive;
}

#endif // WSBROADCASTCLOSEPOLICY_H
