# Human pronunciation and speaker cycling

Prefer licensed natural human recordings for every new entry, ideally 2–3
distinct native Japanese speakers where suitable recordings are available. This
is the user's explicit preference on 2026-10-02, including the six current entries.

- [x] Checkpoint the six-entry cue slice and fix its capture barriers.
- [x] Curate available exact recordings with file-level licences and stated speaker information.
- [x] Record per-entry coverage, edits, signal checks and quality limitations honestly.
- [x] Add multiple cached clips and reveal-only Another speaker, preserving all schedules.
- [x] Support a separate private cache/manifest; exclude it from public packages.
- [x] Test clip selection, missing media, packaging and state preservation.
- [x] Run appropriate aggregate, same-cache smoke and final Release startup; retain failures and costs.
- [x] Deliver coverage, playable samples and updated docs without committing/pushing.
- [ ] Fill the four remaining human-audio gaps and obtain 2–3 verified native speakers per entry where available.
- [ ] Complete human listening review of curated media and macOS input/audio execution.

## Constraints and handoff

No paid services or purchases. No new learning words. が is the subject-marking
particle and いいね remains one approval phrase. Never chop a sentence into a
misleading isolated particle. Do not infer native status or gender from sound.
Commons/Lingua Libre file licences are primary candidates. Forvo listening pages
do not grant unrestricted redistribution or systematic extraction rights; use
authorized interfaces only. Tatoeba audio licences vary by recording.

Personal-use-only media belongs outside source and distributable packages. A
clearly labelled synthetic fallback may remain only where human coverage is
missing; never silently substitute it after human playback failure. Playback,
replay and cycling must not write recall or personal learning evidence.

Completed cue work: [03](../done/03%20demonstrative-and-subject-marker-cues%20-feature.md).
The first 60-entry core/product aggregate passed 56 entries; native-cue capture
failed and three core checks failed (emoji composition, remote terminal initial
state timeout, scope integration selection). Captures are ignored build output.
An iteration found stale log files could satisfy readiness before a new child
rendered. The capture helper now removes its exact prior log/bitmap before launch.
No claimed auditory/native assessment: this tool session has no audio perception
capability; signal and metadata inspection plus playable samples are available.

Root synchronization checkpoint: the user explicitly requested committing and
pushing everything in the Draxul task. This publishes the current capture-helper
fix and pending plan only; the recording-curation implementation remains unfinished.

## Implementation and coverage checkpoint

Deck/artwork schema 4 carries bounded distinct-speaker clip arrays, origin labels
and per-file SHA-256. Runtime defaults: human ことば (Tofugu archive, one voice)
and あさ (Tofugu + CKali). The archive's publisher states native Japanese voices
but supplies no per-file identity; CKali is named by source, native status/gender
unverified. Exact source, licence, edits and coverage: [audio report](../../docs/audio-coverage.md).
No native/gender inference or subjective listening assessment occurred. Four
entries retain explicit synthetic gaps: エージェント, それ, が, いいね.
Commons Ja-sore explicitly says non-native and was not installed as native coverage.
Forvo pages were researched, but no playback URLs extracted, unauthorized downloads,
public media redistribution or paid API access occurred.

An external `audio_directory` selects a provenance-bearing private manifest at
runtime only. It replaces an entry's complete clip list; unavailable files never
silently select bundled human or synthetic audio. Offline N cycling/R replay are
blocked on fronts, during flips and in Help; grading remains visual and explicit.
Private clip changes never rewrite recall state or conversational learning files.

Core + product aggregate: 58/61 entries passed in 246.00 s; all five product
groups passed (14 model cases, five generator cases, nine normal review captures,
14 native-cue captures and three private-audio captures). Native capture 133.05 s;
audio 26.94 s; regular render 85.94 s; generator 4.01 s. The same three core
failures remain: joined emoji, remote terminal initial-state deadline, Windows
scope selection. Configure/build succeeded in 138.32 s combined; phases not
separately measured. A focused audio repeat after strengthening generation-tree
package exclusion passed in 28.13 s. Final review reset the speaker index at every
new reveal and stopped audio when a stale grade selected another front; a product
aggregate is validating that last adjustment.

Final product aggregate after the last adjustment: 5/5 passed in 219.26 s, build
10 steps / 23.30 s without reconfigure. Standard same-cache smoke timed out at
30 s (outer 36.84 s); isolated Debug product startup passed in 2.89 s. Release
build passed in 123.66 s (CMake configure 98.6 s, generate 2.6 s, compilation not
separately timed); initial app startup returned 1 without a retained diagnostic,
then an isolated same-executable diagnostic repeat passed in 2.95 s. Full timings,
failed attempts and host tracker ownership are in `docs/validation.md`. Samples:
`assets/kotoba-tofugu.wav`, `assets/asa-tofugu.wav`, `assets/asa-ckali.wav`.
Actual public speaker UI: `build-ninja-debug/flashcards-render/back.bmp` at the root;
private test captures use Fixture names and are not speaker-curation evidence.

This card remains pending because source coverage/listening/macOS follow-ups are
real open gates. Current audio edits are uncommitted; preserve other chats' work.

## Publication authorization, 2026-10-04

The user now explicitly requests committing and pushing everything. This
supersedes the earlier uncommitted handoff requirement for the implementation,
licensed media, tests and documentation. The unchecked coverage and listening/
macOS tasks remain open; publication does not mark this card complete.
