# Add upcoming cue previews and refresh the current deck

**Summary:** Show a scrollable image-only queue beside the main card in the
scheduler's actual order, add clear English descriptions after reveal, and
refresh vetted offline media for the current vocabulary without resetting review
progress.

- [x] Refresh the current vocabulary and vetted image/audio coverage with provenance.
- [x] Share scheduler ordering with a read-only upcoming queue, keeping both directions and durable progress.
- [x] Add an image-only, scrollable left column with due/future separation and usable narrow layout.
- [x] Include English descriptions on the revealed face only; preserve animated flips, audio and attribution.
- [x] Verify scheduler/answer gating, product aggregate, same-cache smoke and actual normal/narrow screenshots; transfer the default host timeout to existing Draxul card 65.
- [x] Update documentation, media gaps and validation costs; provide the testable build.

Requested 2026-10-06. The user subsequently authorized commit and push. The journal is build input,
never copied into public sources. Previews must contain no target Japanese,
romanization, translation or revealing tooltip. Existing recognition fronts and
production fronts keep their behavior. Viewing/scrolling previews must not grade,
advance a batch or alter schedules. The answer-side English preference supersedes
the former translation-free-back design.

## Implementation and final feedback

All nine journal entries generate eighteen stable directional cards. Added licensed
OpenMoji camera/photo, list and highlighted-calendar scenes; pinned Tofugu archive
human recordings for photograph and today. List has no verified recording. Full
provenance and the remaining five human-audio gaps are in `docs/audio-coverage.md`;
speaker/listening/macOS follow-ups remain owned by [04](../pending/04%20human-pronunciation-and-speaker-cycling%20-feature.md).

The shared read-only queue draws previews without Japanese/romanization/meaning,
including a wordless grammar subject. Actual normal/narrow screenshots and
scrolling/reveal byte checks protect answer separation and durable state.
English descriptions are intentionally public answer-side fields in strict deck
schema 5; journal context and review data remain excluded. Recall schema 1 stays
unchanged.

Additional feedback requested no immediate repetition and preference for struggling
cards. Both directions now share a 600-second cooldown derived from saved
last_reviewed, without rewriting directional schedules. Among eligible due cards:
stage-zero explicit failures, other graded reviews, then new cards; effective due
time and stable deck order break ties. Remembered removes failure priority.
Future/cooldown cards stay Later; small decks may pause. Selection, due count,
next eligibility and queue use the same effective-time rule. The currently shown
card stays pinned until action or invalidation; the locked save rejects stale
opposite-direction grades. Tests cover 599/600-second expiry, reopening,
cross-pane invalidation, same-word-only decks, Again/Remembered and no future pull.

The initial six-group aggregate passed before the scheduling feedback. A focused
policy run passed the new scenarios but exposed two obsolete fixture expectations;
those were corrected before the final aggregate. Tests which need to examine an
opposite face seed an expired timestamp in isolated state rather than sleep or
change user progress. Exact results/costs are recorded in `docs/validation.md`.

Final synchronized-host aggregate: 6/6 passed in 288.84 s, build 33.42 s. Standard
Debug smoke timed out (30-second bound, outer 34.48 s); isolated Flashcards startup
passed in 2.475 s. The unresolved default host gate is transferred to
[Draxul 65](../../../../kanban/pending/65%20windows-validation-timing%20-test.md).
Release build/startup exited 0 (151.23 s build, 159.83 s outer). Actual 900x760 and
620x720 backs and all three new cue backs were inspected. Build:
`D:/dev/Draxul/build-ninja-debug/draxul.exe --plugin dev.draxul.flashcards`.
Local binaries/screenshots, personal journal input and private state remain ignored.
