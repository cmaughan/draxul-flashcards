# Draggable queue and immediate review rounds

Remove the redundant Up/Down buttons and let the user drag the queue scrollbar.
Allow an explicit additional review round without waiting for the automatic
schedule or shared word cooldown. Ordinary scheduled reviews retain their rules.

**Priority:** P2

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

## Windows gate preparation, 2026-10-08

Read-only inspection confirmed the existing Ninja Debug registration selects
exactly one `draxul-render-flashcards-queue` test. It launches
`plugins/flashcards/tests/queue_smoke.py` against the same-cache `draxul.exe`,
using the configured vocabulary source and isolated temporary recall state.
The shared native helper is `plugins/flashcards/tests/render_smoke.py`; there is
no root `tests/render_smoke.py`. No harness change is justified by inspection
alone, and no assertions have been removed or weakened.

After the parent grants the exclusive GPU/native-pointer slot, run from the
Draxul checkout without configuring or building:

```powershell
ctest --test-dir D:/dev/Draxul/build-ninja-debug --parallel 1 --no-tests=error --output-on-failure -R '^draxul-render-flashcards-queue$' --output-log D:/dev/Draxul/build-ninja-debug/flashcards-queue-gate.log
```

The existing gate exercises 900x760 and 620x720 native Windows input, narrow
drag/bottom-track endpoint equality, normal drag/return-to-top and track clicks,
byte-identical recall during preview/reveal/scroll/start, unchanged cue-only
previews on reveal, and exactly one explicit immediate-round grade within the
shared cooldown without changing unrelated directions. Inspect the resulting
normal/narrow fronts/backs, scroll/reset/track endpoints, completed narrow face,
Review again front and repeated-grade capture under
`build-ninja-debug/flashcards-queue/` before checking the remaining gate.

CTest's GPU resource lock does not coordinate separate CTest processes: parent
scheduling and an idle desktop pointer remain required. Preparation ran only
CTest discovery (`-N -V`, one registered test, no execution); no build, native
input, smoke or render pass has run in this follow-up. The parent owns the
current aggregate/build and shared startup gates. Keep this card pending until
the scheduled native run and capture inspection actually pass.

## Exclusive Windows gate result, 2026-10-08

After the parent explicitly granted the GPU/pointer slot and reported other
clients/servers closed and Release compilation finished, the command above ran
once with unchanged assertions and the existing 180-second CTest bound. It
failed in 50.15 s (50.18 s total CTest), at the first narrow drag:

```text
render_smoke.py:141
RuntimeError: Desktop pointer moved during the synthetic drag; rerun with the mouse idle
```

This establishes that the helper's observed desktop cursor differed from its
saved drag origin; it does not establish who moved it or prove a product defect.
No retry-to-green, rebuild, native diagnostic rerun, weakened assertion or
production/harness modification was made. The failed child's cleanup completed;
a process query found no `draxul.exe` or `draxul-server.exe`, and the exclusive
slot was released immediately before further capture inspection/documentation.

Four fresh captures were inspected at their exact sizes: normal front/back
(900x760) and narrow front/back (620x720). The image-only queue stays visually
unchanged on reveal, the main answer/controls fit, and neither front is the
unexpected Help face from earlier failures. The run passed byte-identical recall
checks for both layouts and the normal cue-column pixel equality check before
failing during `queue-narrow-scroll`. That drag produced no new bitmap. Narrow
endpoint equality, normal drag/return/track, immediate-round start and explicit
repeat grading were not reached, so the remaining checkbox stays unchecked.

Evidence is retained in `build-ninja-debug/flashcards-queue-gate.log` and
`build-ninja-debug/flashcards-queue/queue-narrow-scroll.log`. The four inspected
BMPs also have `-gate.png` inspection copies beside them. Other endpoint/repeat
artifacts dated 2026-10-06 are stale and are not evidence for this attempt.
The screenshots show CPU indicators between 63% and 100%; this is an observation,
not a diagnosed cause of the pointer failure. PNG inspection used Windows
System.Drawing after the default Python lacked Pillow; no dependencies installed.

Parent-reported shared validation: behavior aggregate 73/79, all three Flashcards
behavior suites passed, isolated same-cache Debug startup passed, and five
renderer screenshots passed. Standard default-profile startup still failed and
remains owned by Draxul `65 windows-validation-timing -test.md`. These reports do
not replace the failed native queue gate. This follow-up performed no configure,
compilation, aggregate rerun, smoke rerun, Release run or remote CI. Only this
card changed; no commits were created.
