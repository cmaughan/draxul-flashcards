# Embedded Japanese flashcards

Design: [embedded-japanese-deck.md](../../plans/embedded-japanese-deck.md).

- [x] Inspect SDK/product mounting and preserve unrelated dirty checkout changes.
- [x] Record visible implementation plan and public repository name choice.
- [x] Implement every-build deck validation/embedding and privacy-safe fixture.
- [x] Implement durable explicit recall grading and due-card selection.
- [x] Implement keyboard/mouse host and correct front/back/empty/error states.
- [x] Cache licensed artwork and Japanese font; verify visible rendering.
- [x] Pass product aggregate and plugin smoke; record core aggregate and validation costs.
- [x] Explicitly transfer the standard smoke/core failures to their existing host cards, linked below.
- [x] Document launch/configuration/manual checks and limitations.
- [x] Authorize initial commits, create public repository and adopt as submodule.

## Constraints

Personal vocabulary is build input only. Generated decks belong in the ignored
build tree; never commit/distribute a personal binary. Runtime uses the embedded
snapshot. Journal builds are read-only. Scored recall never changes conversational
exposure progress. Preserve Personal Assistant and all other dirty work.

## Progress

2026-10-01: Plan recorded after inspecting product registration, NanoVG's dual
backend adapter and SDK plugin-scope atomic storage. Proposed local product root
is `plugins/flashcards`; public repository name/visibility confirmed by the user.
The user authorized committing and pushing everything on 2026-10-01. The public
repository is created, initial commit `f826a3c` is pushed, and Draxul has registered
the product as a submodule at `plugins/flashcards`.

Implemented translation-free recognition as requested: kana front, licensed image
association on back, optional image hint recorded as assisted. No English meaning
or conversation fields enter the embedded payload. Chose pinned Twemoji art and
static Noto Sans JP Regular after runtime capture exposed the variable font's kana
rendering problem. No build/runtime artwork fetch occurs.

Review persistence uses SDK atomic plugin-scope storage and a cross-process lock;
each grade rereads state and detects stale panes before writing. Explicit remembered,
forgotten and assisted counters remain separate from the conversational journal.
Intervals, 10-minute retry and 20-grade batches are documented design choices.

All three product CTest entries pass, including actual keyboard/mouse input, SDK
state, reopen and corrupt-state preservation. Core selection is 57/59 because of
existing host failures. Two standard smoke attempts timed out; direct same-cache
plugin smoke and final Release plugin startup passed.
See [validation evidence and costs](../../docs/validation.md). macOS execution was
unavailable; shared Metal callback and POSIX locking path are implemented.

The capture harness needed first-frame synchronization, log-pipe deadlock removal,
and physical-pixel mouse coordinates. Diagnostic repetitions are disclosed in the
validation record. Remaining human review: resizing, multiple panes and Metal input.
This product slice is complete. Host failures retain their existing owners:

- [63 joined-family-emoji-fallback -bug.md](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/63%20joined-family-emoji-fallback%20-bug.md): the font shaping assertion.
- [64 windows-test-scope-selection -bug.md](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/64%20windows-test-scope-selection%20-bug.md): real fixture scope selection.
- [65 windows-validation-timing -test.md](https://github.com/cmaughan/Draxul/blob/main/kanban/pending/65%20windows-validation-timing%20-test.md): recurring standard host smoke timeout; the Flashcards evidence was added to this card.

These transfers preserve the unresolved host gates rather than claiming they
passed. Publication includes source and licensed assets only; no personal build
output or journal was committed. Manual review suggestions and unavailable macOS
execution remain disclosed in the validation record.
