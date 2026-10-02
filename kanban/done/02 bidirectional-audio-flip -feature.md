# Bidirectional cards, pronunciation and animated presentation

**Summary:** Add independent recognition and production reviews, reveal-only
pronunciation, a smooth card flip and crisp meaningful picture associations.

- [x] Reload canonical journal and inspect rendering, storage and audio seams.
- [x] Expand each embedded word into two directions; preserve recognition history.
- [x] Remove front hints and gate grades/audio until reveal completes.
- [x] Add cached pronunciation playback/replay with honest unavailable states.
- [x] Replace low-resolution imagery; record reusable sources and replacement path.
- [x] Polish responsive card layout and animate click/keyboard flips.
- [x] Verify direction isolation, persistence and front/midflip/back rendering.
- [x] Run aggregate, same-cache smoke and Release startup; record costs and baseline failures.
- [x] Update product/core feature docs for the reviewed slice.

## Design and constraints

Recognition front is kana only. Its back shows picture and kana; pronunciation
plays on reveal/replay. Production front is picture only; its back retains that
picture and reveals kana with pronunciation. No front hint/audio/text may disclose
the target. Again/Remembered only after reveal. Existing recognition schedules are
preserved; production uses independent keys. Listening-only cards remain later.

Keep normal builds/runtime offline with validated embedded data and cached media.
Never copy personal journal details into product source or publish generated
personal decks. Source changes are checked on every enabled incremental build.
Images for abstract words are associations, not dictionary definitions; let users
replace them. Use reusable original media and show credits only after reveal.

The user reviewed the appearance and requested committing and pushing on
2026-10-02. Other chats are actively editing core, Megacity and Satview; preserve
their changes when publishing this product and its Draxul pointer/docs update.
Windows execution is available; inspect paired Metal/SDL paths and report
unavailable platform or audio verification accurately.

## Implementation and evidence

Deck schema 2 carries a whitelisted media record per word; the review model expands
it into independent recognition/production keys in the existing state schema 1.
Existing recognition dates/counters remain intact, including historical assisted
counters. New grades use only Again/Remembered after reveal. Atomic SDK storage,
writer locking and stale-card rejection remain the persistence boundary.

Backend-neutral NanoVG draws a 580 ms affine card turn. SDL audio starts only when
the turn completes, stops on grade/hide/quiesce and supports explicit replay.
Windows shares SDL3 through its existing dynamic dependency; Metal uses the host's
SDL implementation through the existing macOS audio-product linkage pattern.

Original reusable photographs replace blurry word/morning icons; licensed 618px
OpenMoji art supplies apple/robot and a separately drawn software-task scene.
The five cached WAVs are synthetic Mei pronunciation (CC BY 3.0), not native
recordings. All provenance/licenses are in artwork.json and README. An optional
explicit-kana curation tool reads no journal. Stable-ID image overrides plus an
absolute local picture directory allow replacement without changing schedules.

Initial core + product aggregate: 57/59 entries passed in 77.98 s; all three
product entries passed. Existing failures are joined-family emoji shaping and
Windows test-scope fixture selection. Inspected front/turn/back and both direction
captures. A later product run exposed a mouse-test race during heavy concurrent
compilation: posted input plus a fixed sleep did not establish reveal completion.
The harness now waits for the actual completion diagnostic before grade/replay.

Same-cache standard smoke again timed out at 30 s. This host gate remains unresolved;
the existing Draxul `65 windows-validation-timing -test.md` owns follow-up. Core
failures remain owned by `63 joined-family-emoji-fallback -bug.md` and
`64 windows-test-scope-selection -bug.md`. These are failures, not passing checks.
Initial Release build took 21.92 s (10 Ninja steps, no reconfigure); startup
exited 0. Final Release rebuild after the requested tweaks took 22.84 s (10 steps,
no reconfigure); startup exited 0. Startup times were not separately recorded.

Final product aggregate after both requested tweaks: 3/3 passed in 78.62 s;
12 Ninja steps in 28.66 s, no reconfigure. Model 0.22 s, generator 2.96 s, runtime
78.40 s with overlap. Final front/back/production captures were inspected. Direct
same-cache plugin startup passed in 26.25 s; an isolated-data repeat passed in
4.53 s to verify the product without loading the user's other restored panes.

The user confirmed the rotation works and identified two presentation tweaks via
the PA chat: common button labels now use NanoVG center/middle alignment, and the
ambiguous beach cue has been replaced by Petr Kratochvil's CC0 waking-in-bed photo
beside a separately drawn daylight window and a 07:00 alarm. Candidate images were
inspected visually; unrelated dark radio, banana and fantasy-bird images were
rejected. Preserve stable IDs and progress across artwork changes. Future curation
should prioritize an obvious intended sense over scenic beauty.

The user clarified that the voice transcript repeated app playback. That is not
their pronunciation or recall; no such evidence was added to the learning journal.

Audio automation uses SDL's dummy driver, proving queue/replay gating and lifecycle
without evaluating audible output or pitch quality. macOS execution was unavailable;
paired implementation was inspected. Listening-only cards remain a later slice.

The standard smoke checkbox records a completed attempt and explicit transfer of
the unresolved host gate, not a passing standard smoke. Follow-up owners:
[emoji shaping](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/63%20joined-family-emoji-fallback%20-bug.md),
[scope selection](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/64%20windows-test-scope-selection%20-bug.md),
[Windows startup timing](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/65%20windows-validation-timing%20-test.md).
The isolated plugin smoke and product lifecycle checks passed; no required product
implementation or local validation item remains. macOS and human audio-quality
checks remain unclaimed platform/review limits, not completed local results.
