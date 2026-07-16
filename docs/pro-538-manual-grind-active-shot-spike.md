# PRO-538 Spike - Persist Dashboard manual grind to active-shot notes

**Verdict:** the current device notes protocol cannot safely implement this as a web-only change. No production web behavior is changed by this spike.

## What was verified

The Dashboard manual GRIND commit can identify an active shot from
`machine.value.status.process` (`id` plus active flag `a`), and it can invoke the
existing notes path. `NotesService.saveNotes()` first reads
`req:history:notes:get` and sends a merged document through
`req:history:notes:save`.

The firmware handler, however, unconditionally replaces the shot's notes file
with the submitted document. It has no compare-and-set, revision, or
"set `grindSetting` only if empty" condition. The GET and SAVE are separate
WebSocket requests.

## Required policy

A safe implementation must:

1. Attach only for an active process with an ID at the moment the Dashboard
   commit is handled. Commits before a shot, after it ends, or after its ID is
   cleared must not write notes.
2. Treat non-empty persisted `notes.grindSetting` as user-owned. A manual
   Dashboard commit must not replace it.
3. Be idempotent for repeated commits: after a value is attached, later commits
   must neither replace that saved value nor create a different write.
4. Retain the current per-browser manual-grind event log and Shot Notes
   inference precedence for shots that do not have a persisted manual value.

## Why a web-only read/check/write is unsafe

A proposed browser sequence would be:

```
GET notes -> see empty grindSetting -> SAVE merged notes with dashboard value
```

Between those two requests, a user can save Shot Notes (from this or another
browser) with an explicit `grindSetting`. The subsequent Dashboard SAVE would
merge only its stale GET result and overwrite that explicit value. A per-tab
promise queue or a hook ref can prevent duplicate writes from that tab, but
cannot protect a concurrent browser or device request. Therefore it does not
meet the required "never overwrite" rule.

## Smallest viable follow-up

Add an atomic conditional operation to the device protocol, for example
`req:history:notes:save` with an explicit `setIfEmpty: ["grindSetting"]`
contract (or a dedicated conditional set request). The firmware must load the
current notes and, in the same request handler, retain a non-empty current
`grindSetting`; only then may it merge the Dashboard value and write the file.
The response should state whether the field was attached, already present, or
no active shot matched.

With that contract, the web hook can expose an active-shot manual-grind commit
callback. It should call the conditional operation only for `process.a` and a
non-empty positive manual setting. It must leave
`recordManualGrindSetting()` and `inferGrindSettingForShot()` unchanged.

## Test plan for the follow-up

Focused protocol/web tests should cover:

- no active process and inactive/finished process: no notes request;
- active shot with empty `grindSetting`: attaches the committed value;
- repeated commits for the same shot: first value remains and later commits are
  no-ops;
- an existing explicit `grindSetting`: preserves that value;
- an explicit save racing the conditional attach: device preserves the explicit
  value regardless of request order;
- a shot with no saved manual value: current localStorage event and inference
  precedence remain unchanged.

## Assumption

`grindSetting` is the user-editable Shot Notes field, so any non-empty persisted
value is explicit/user-owned. This is stricter than treating a number as a
Dashboard-derived value, and is required to avoid reinterpreting or clobbering
existing notes.

## On-device verification

Carlos will perform the WebSocket smoke verification after the protocol follow-up
is merged: commit GRIND before, during, and after a shot, then verify a manually
saved Shot Notes value wins across browser clients.
