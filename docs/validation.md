# Launch and validation

Run commands from the Draxul checkout containing this mounted product. On macOS,
replace `py` with `python3`.

```powershell
py do.py run debug -- --plugin dev.draxul.flashcards
py do.py test debug --flashcards
py do.py smoke --skip-build
py do.py run release --console -- --plugin dev.draxul.flashcards --smoke-test
```

For a personal source, set `DRAXUL_FLASHCARDS_VOCABULARY_SOURCE` to your absolute
journal filename before configuration. The environment override replaces the
cached source; later incremental builds check that retained source every time.
For public/CI builds, explicitly choose `tests/fixtures/vocabulary.json` in this
product. Never upload personal generated headers, modules, captures or binaries.

## Automated boundaries

The model suite uses deterministic clocks and injected persistent storage to
exercise explicit grading, all intervals, independent directions, retry scheduling,
reopen/deck additions/removals, save failures, corrupt state, stale overlapping
panes, writer exclusion, missing images and the 20-grade batch limit.

The generator suite uses public fixture data and a real temporary CMake/Ninja
project. It verifies source changes and missing sources on incremental builds
without reconfiguration, unchanged output timestamps, read-only source handling,
private-field exclusion, duplicate/schema/bounds/conflict failures and kana
preservation.

The runtime suite loads the actual packaged module and captures front/turn/back,
recognition and production, keyboard Remembered, mouse Again, reopened schedule and corrupt-state
screens. Its Windows lifecycle checks require at least two image-backed cards;
the public fixture supplies them. It isolates child APPDATA/LOCALAPPDATA, targets
the exact child SDL window and checks the actual SDK state file. For mouse input
it briefly positions the pointer inside that window, restoring it if the user
has not moved it. It does not click unrelated windows. Captures stay in the ignored
host build tree. The compiled deck follows the selected build source, so personal
captures are also private.

It verifies no front or mid-turn audio/grades, queues pronunciation on reveal and
replay through SDL's dummy audio driver, and checks that production grades leave
recognition records unchanged. Audio queue success does not prove audible output
or assess pronunciation quality.

The non-Windows runtime harness currently verifies the rendered front; native
keyboard/mouse automation is Windows-only. Shared model/generator tests run on
both platforms. The plugin implements both Vulkan and Metal using the existing
NanoVG support seam; macOS execution was unavailable in this session.

## Local evidence, 2026-10-01

Windows, Ninja Debug throughout iteration; Release used only for the required
final startup confirmation. The selected personal journal was read-only build
input. Product source and public fixtures contain no machine-local journal path.

| Gate | Result and measured cost |
|---|---|
| Initial Debug configure/build | Passed, 723 Ninja steps, 189.7 s combined. Configure/generate and compilation were not timed separately. Much of this rebuilt the existing dirty host checkout. |
| Incremental edits | Passed. Multiple builds and capture diagnoses were needed for the static kana font, readiness synchronization and Windows mouse/DPI handling; individual timings were not retained. |
| Core + product aggregate | 57/59 CTest entries passed, 63.36 s. All three product entries passed. Existing core failures: joined-family emoji shaping and Windows test-scope fixture selection. |
| Final product aggregate | 3/3 passed, 38.42 s. Model 0.16 s, generator 1.79 s, packaged runtime 38.29 s, with parallel overlap. |
| Same-cache standard smoke | Two attempts timed out at the wrapper's 30 s limit; the retry retained a renderer-startup trace. This is an unresolved host gate. |
| Same-cache plugin smoke | Direct Debug plugin startup exited 0 within a 30 s bound; exact elapsed time was not retained. |
| Final Release build/startup | Passed, 98 Ninja steps, 130.54 s combined configure/build/startup. The command loaded the flashcard plugin and exited 0. Phase timings were not separated. |
| Remote CI / macOS | Not run during local validation; publication does not establish those results. |

Repeated validation was diagnostic: the first full selection exposed a new
capture-input readiness failure, fixed before the recorded 57/59 run. A later
product-only attempt exposed mouse coordinates/DPI handling, fixed before the
final 3/3 run. Additional direct captures investigated those failures. No golden
references were blessed and no full render inventory was rerun. Standard smoke
and Release compilation overlapped; the successful plugin smoke also ran during
Release compilation.

Existing host follow-ups remain in Draxul's `kanban/pending/`:
`63 joined-family-emoji-fallback -bug.md`,
`64 windows-test-scope-selection -bug.md`, and
`65 windows-validation-timing -test.md`. They are not fixed by this product.

## Interactive review

1. Launch with the public fixture or your configured journal. Verify kana-only
   recognition and picture-only production fronts, animated flip, picture/kana/
   romanization back, replay and visible attribution. Listen to the synthetic audio.
2. Grade using keyboard and mouse; reopen and rebuild to check retained independent due dates.
   Opening/flipping should not alter grade counters.
3. Use an ID without mapped artwork: flip, verify grading is unavailable, then Skip.
4. Resize/switch panes, check focus and controls, and repeat on macOS/Metal.

Steps 1–2 are covered by Windows packaged automation and inspected captures.
Missing-image behavior is covered by the model; interactive resizing, multi-pane
use and macOS input/render review remain useful human checks, not claimed results.

The user authorized committing and pushing the full checkout on 2026-10-01.
The public [draxul-flashcards repository](https://github.com/cmaughan/draxul-flashcards)
contains source, public fixtures and licensed assets; the initial implementation
commit is `f826a3c`. Draxul registers it at `plugins/flashcards` as a Git submodule.
No personal generated deck, module, binary or capture was published.

The completed product card records explicit transfer of unresolved host validation
to the existing Draxul cards. These remain failures, rather than passing checks.

## Bidirectional/audio slice, 2026-10-02

Each word now has separate recognition and production schedules. Front-side audio,
hints and grades are blocked; reveal completes the animation before pronunciation
or grades become available. Recognition history is retained. Reusable photographs
and 618px OpenMoji assets replace low-resolution icons; five licensed Mei WAVs are
cached, explicitly labeled synthetic, and have no normal-build/runtime network
dependency. The user confirmed the rotation and requested centered button labels
and a clearer morning cue. Inspected final captures show middle-aligned button
labels and a waking-in-bed photograph beside daylight and a 07:00 alarm.

| Gate | Result and measured cost |
|---|---|
| Initial Debug configure/build | Failed on a log-level enum typo; 93.32 s combined, 15 Ninja steps. Corrected before validation. Configure/build phase times were not separated. |
| Core + Flashcards aggregate | Build passed in 16.94 s; 57/59 CTest entries passed in 77.98 s. All three product entries passed. The same two core baseline failures from the first slice remained. |
| Product iteration | 281 Ninja steps, 65.76 s build, then 2/3 tests passed in 43.03 s. The mouse capture exposed an input/reveal timing race under concurrent host compilation. |
| Final product aggregate after review tweaks | 12 Ninja steps, 28.66 s build; 3/3 tests passed in 78.62 s (model 0.22 s, generator 2.96 s, runtime 78.40 s, with overlap). No reconfigure. |
| Standard same-cache smoke | Timed out at the wrapper's 30 s bound; retained log. Existing host gate remains unresolved. |
| Same-cache plugin startup | Passed, exit 0 in 26.25 s with the user's restored space. An isolated-data repeat passed in 4.53 s and verified the product alone; it did not replace or rewrite user state. |
| Initial Release startup | Passed; 10 Ninja steps, 21.92 s build, no reconfigure; startup exited 0, its elapsed time was not separately recorded. Repeated only after the user requested presentation changes. |
| Final Release startup after review tweaks | Passed; 10 Ninja steps, 22.84 s build, no reconfigure; startup exited 0, its elapsed time was not separately recorded. |
| macOS / auditory quality | macOS execution unavailable. SDL dummy-driver automation establishes queuing/replay, not audible output or pronunciation quality. The user reported hearing app playback; no user pronunciation/recall assessment was inferred. |

The final harness waits for the host's actual reveal-completion diagnostic rather
than treating a posted key and fixed delay as proof. It uses a longer capture
window to allow input processing under load. Captures and generated personal decks
remain in ignored build trees; none are part of the published source.

Core validation was not repeated by this slice after edits limited to presentation
and the product harness. Other chats were concurrently editing/testing core and
two other products in the shared checkout; the runner's lease prevented overlapping
build mutations. Two product commands were rejected while those builds owned the
cache. Only this product and its Draxul docs/pointer are included in this publication.

Unresolved host gates are explicitly transferred to the existing Draxul cards
`63 joined-family-emoji-fallback -bug.md`, `64 windows-test-scope-selection -bug.md`
and `65 windows-validation-timing -test.md`. No platform gate is reported as passed
without execution, and no render references were blessed.

## Native cues and human audio checkpoint, 2026-10-02

Native NanoVG cues now cover the requested それ, が and whole phrase いいね.
The English guide explains them one at a time before reviews and persists its
acknowledgment independently from SRS. The production grammar chip is blank;
the back reveals が immediately after それ. Known-only diagram dependencies are
validated at generation and parse time. The actual journal currently produces
six word records / twelve independent directions, without private meanings,
conversation details or journal review data in the generated deck.

Initial configure/build succeeded in 95.60 s combined (phases not separately
measured). The core/product aggregate passed 56/60 entries in 150.85 s: model,
generator and standard packaged review passed. Native-cue capture failed on a
reveal deadline; an iteration found old log diagnostics could satisfy readiness
before a fresh child rendered. Exact prior log/bitmap files are now removed before
launch. Another run reached the host's "Failed to capture screenshot" diagnostic.
These failures were retained, not counted as passes.

The initial core failures were joined-family emoji composition, Windows scope
selection and a remote-terminal initial-state deadline. Other chats own active
core changes and their follow-ups; this product does not change those systems.

The user then requested human audio and speaker cycling. Source/language/licence
evidence, actual coverage and quality limits live in [audio-coverage.md](audio-coverage.md).
The human recordings are complete cached words, with no pitch/speed/trim edits.
Private audio is configured at runtime outside the package and generated headers;
missing human clips never silently substitute synthetic audio. Reveal, replay,
speaker switching and help do not write review success.

Windows packaged tests use SDL's dummy audio driver. No subjective listening,
speaker-native assessment or user pronunciation evidence is inferred. macOS/Metal
execution is unavailable on this host. New audio implementation remains uncommitted;
another root chat published the earlier cue/capture checkpoint while this slice
was still being implemented.

## Final human-audio handoff, 2026-10-02

| Gate | Result and measured cost |
|---|---|
| Debug configure/build before core/product aggregate | Passed, 138.32 s combined; phases not separately measured. |
| Core + Flashcards aggregate | 58/61 CTest entries passed, 246.00 s (runner 246.19 s). All five product groups passed; three core failures remained: joined emoji, remote terminal initial state and Windows scope selection. |
| Focused package-exclusion repeat | Passed, 1/1 in 28.13 s, after strengthening the assertion to inspect every packaged generation. This repeated only the private-audio runtime group. |
| Final Debug build | Passed, 10 Ninja steps, 23.30 s; no reconfigure. Reset speaker selection on new reveals and stop previous audio when a stale grade returns to a front. |
| Final product aggregate | Passed, 5/5 in 219.26 s (runner 219.42 s): 14 model cases, five generator cases, nine normal captures, 14 native-cue captures and three private-audio captures. Native cues 121.51 s, normal render 72.07 s, private audio 25.64 s; generator/model overlapped other work. This repeats product coverage after the final code adjustment, without rerunning unrelated core tests. |
| Standard same-cache Debug smoke | Failed at the 30 s bound, owned PID 53680 stopped by the wrapper; outer elapsed 36.84 s. Existing default-profile host investigation remains open. |
| Same-cache isolated Flashcards startup | Passed, exit 0 in 2.89 s; fresh temporary APPDATA/LOCALAPPDATA, SDL dummy audio, no user-state changes. This does not replace the failed standard smoke. |
| Final Release build | Passed, 13 Ninja steps, 123.66 s combined. Ninja regenerated CMake: configure 98.6 s, generate 2.6 s; compilation alone was not separately timed. |
| Release startup and diagnostic repeat | Initial `do.py run release --console -- --plugin dev.draxul.flashcards --smoke-test` returned 1 after the successful build, with no retained app diagnostic (outer command 125.94 s). A fresh isolated launch of that same Release executable with a retained log passed, exit 0 in 2.95 s, showing Vulkan swapchain and Flashcards frame readiness. No cause is inferred for the first failure. |
| macOS / audible quality / remote CI | Not executed or claimed. Windows dummy-driver tests verify queueing and selection, not listening quality. Curated clips require human listening review; 2–3 verified native voices and four missing human entries remain open. No remote CI was started for uncommitted changes. |

The host failures are explicitly retained under the existing Draxul cards
`63 joined-family-emoji-fallback -bug.md`, `64 windows-test-scope-selection -bug.md`
and `65 windows-validation-timing -test.md` (including remote initial-state and
default-profile smoke timing). Natural coverage and listening/macOS review stay
in the pending human-audio card. The diagram implementation is complete. Human
audio changes, source licences, tests, tracker updates and documentation remain
uncommitted and unpushed under this slice's instruction.

## Printed-word visual replacement, 2026-10-02

The user approved replacing the conversation cue for ことば with a photograph of
one printed word. Licensed source and display adaptation are recorded in
[printed-word-cue.md](printed-word-cue.md). The first capture showed the outline
clipping the bottom half of the text; user feedback confirmed it. The display
focus/angle and outline height were corrected. Rebuilt front and back were then
inspected at actual 900x760 card size: complete letters/punctuation and padding
fit inside the outline. Production shows no target kana or English translation.
The card back retains human playback and source credits. Two capture iterations
(four captures total) used temporary state and confirmed reveal preserved its bytes;
capture durations were not separately measured.

| Gate | Result and measured cost |
|---|---|
| Visual iteration builds | First build: 10 steps, passed, elapsed not retained separately. Corrected build: 10 steps, passed in 13.56 s. No reconfigure. |
| Product aggregate build | Passed, eight check/staging steps, 13.25 s; no reconfigure and no compilation needed after the corrected visual build. |
| Product aggregate | Passed, 5/5 CTest groups, 194.53 s (runner 194.66 s). Model 0.20 s, generator 2.27 s, 14 native captures 107.08 s, nine normal captures 64.47 s, three private-audio captures 22.96 s. Model/generator overlap other tests. |
| Standard same-cache Debug smoke | Passed, outer 27.79 s. Prior timeouts remain historical failures; this pass does not establish their cause or fix. |
| Final Release | Passed: ten build steps, 13.34 s, no reconfigure; isolated plugin startup exited 0 with swapchain/frame readiness logged. Outer build/startup 15.48 s; startup alone not separately timed. |
| macOS / remote CI / broader core inventory | Not repeated for this product visual change. Cross-platform NanoVG path is shared. Existing core failures are not claimed resolved. No CI triggered for uncommitted work. |

The completed photographic slice and the earlier human-audio implementation
remain uncommitted/unpushed. Real schedules, stable IDs, audio selections and
conversational learning files were preserved.

## Publication, 2026-10-04

The user explicitly requested committing and pushing all pending work. This
supersedes the earlier instructions to retain these slices uncommitted. Publish
the licensed human audio, speaker selection/private-cache support, corrected
printed-word cue, provenance, tests and tracker updates together. The validation
above remains the evidence for the unchanged implementation; no redundant build
or tests are required for this Git-only publication. Missing natural-speaker
coverage and human listening/macOS review stay pending in the human-audio card.
Generated personal decks, private caches, binaries and screenshots remain ignored.
