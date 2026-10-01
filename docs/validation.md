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
exercise explicit grading, all intervals, hint penalties, retry scheduling,
reopen/deck additions/removals, save failures, corrupt state, stale overlapping
panes, writer exclusion, missing images and the 20-grade batch limit.

The generator suite uses public fixture data and a real temporary CMake/Ninja
project. It verifies source changes and missing sources on incremental builds
without reconfiguration, unchanged output timestamps, read-only source handling,
private-field exclusion, duplicate/schema/bounds/conflict failures and kana
preservation.

The runtime suite loads the actual packaged module and captures the front/back,
keyboard Remembered, mouse Forgot, reopened schedule, completion and corrupt-state
screens. Its Windows lifecycle checks require at least two image-backed cards;
the public fixture supplies them. It isolates child APPDATA/LOCALAPPDATA, targets
the exact child SDL window and checks the actual SDK state file. For mouse input
it briefly positions the pointer inside that window, restoring it if the user
has not moved it. It does not click unrelated windows. Captures stay in the ignored
host build tree. The compiled deck follows the selected build source, so personal
captures are also private.

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
   front, image hint, flip, kana/romanization back and visible attribution.
2. Grade using keyboard and mouse; reopen and rebuild to check retained due dates.
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
