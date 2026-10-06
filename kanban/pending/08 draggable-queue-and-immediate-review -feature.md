# Draggable queue and immediate review rounds

Remove the redundant Up/Down buttons and let the user drag the queue scrollbar.
Allow an explicit additional review round without waiting for the automatic
schedule or shared word cooldown. Ordinary scheduled reviews retain their rules.

- [x] Implement thumb dragging, track clicks, release/cancellation and wheel scrolling.
- [x] Add Review again with a bounded round, explicit persisted grades and stale-grade protection.
- [ ] Exercise real normal/narrow pointer input and immediate repeat reviews; inspect renders.
- [x] Run the Flashcards aggregate, same-cache startup smoke and Release startup; record costs and limitations.
- [x] Update product documentation and root feature reference.

Starting a round and scrolling are read-only. Only explicit Again/Remembered
actions update existing direction scores and shared review history. Each
direction appears at most once per additional round, with the existing 20-grade
session limit. Reopening starts with the normal due-only schedule.

The override is local session state; queue due dates and due counts retain the
actual schedule. Additional rounds compare both directional progress records
under the existing lock before accepting a grade. Imported peer results retain
the revealed face and reject a stale answer. Explicit repeat grades use the
existing durable shared-event path, including convergence on a fresh device.

Initial model iteration passed 723 assertions / 32 cases (4.44 s). Native queue
diagnosis passed normal/narrow drags, track click, restoring the top and exactly
one explicit cooldown grade. The Windows capture helper now activates its exact
owned SDL window before input, sends a paired final motion/release to that child,
and offers a single click for controls replaced by the first action. It does not
physically hold the desktop mouse button while other pointer movement is present.
Expanded captures check that a narrow drag clamps like a bottom track click and
starting a round leaves its answer unrevealed. The queue gate remains pending:
other desktop pointer movement interrupts synthetic drags. A focused trace
visually reached the exact narrow endpoint, but the full queue retry then failed
its return-to-top assertion (62.91 s). Obtain an idle pointer for native input
verification before claiming this expanded group passed or moving the card to
done. The latest helper uses only window-directed button messages.
Its latest queue run failed before drag input because the first front unexpectedly
captured Help again (15.81 s). The reason is not established. The helper reports
physical pointer movement during a controlled drag explicitly. Do not discard
the preview comparison or endpoint assertions to obtain a passing result.

Model, generator, reader, normal render, audio, exports and cue groups passed
across the aggregate and cue rerun. Debug Flashcards startup passed in 20.08 s;
Release build/startup passed (94.50 s build). Standard Debug host smoke again
timed out at 30 s; this inherited host issue stays with Draxul card 65.
Costs and platform limitations are in [validation.md](../../docs/validation.md).
