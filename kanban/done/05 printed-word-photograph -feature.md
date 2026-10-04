# Printed word photograph for ことば

Replace the conversation scene with a licensed photograph of a printed page.
One actual printed word should stay sharp and visibly outlined; surrounding text
should be subdued. Do not show the target spelling or its translation in a
picture-first review. Preserve stable IDs, both schedules and current audio.

- [x] Inspect existing media, presentation and product workflow.
- [x] Select and record suitable licensed photography and any display edits.
- [x] Replace the default cue and render a crisp focus treatment.
- [x] Rebuild, inspect the actual production front/back and run product aggregate/startup gates.
- [x] Update provenance/documentation and deliver the rebuilt screenshot without commit/push.

User approval arrived through the PA chat on 2026-10-02. This is a visual change
only; no new learning entries, review-state changes or publication is authorized.
Earlier uncommitted human-audio work must remain intact. Keep photographic source
pixels and annotate the selected word in the renderer when that improves clarity.

## Implementation / visual review

Cached Kasharp's original `Words on a Page.png` under CC BY-SA 4.0, with exact
source, hash and display-edit provenance in `docs/printed-word-cue.md`. The default
mapping now selects `printed-word.png`; removed the unused conversation photograph.
An asset-specific NanoVG display crop levels the printed word, fades the surround
and draws a gold outline. Custom images retain their own presentation. Neither
the Japanese target nor its English translation is added to the production front.

Initial rebuilt capture and user feedback both showed the top-half alignment bug.
Adjusted source focus from (1290,3030) to (1275,3060), photo rotation from -0.95 to
-0.85 radians and the focus window from 202x76 to 202x96 logical pixels. The second
rebuilt front and back were inspected at 900x760: every letter and punctuation
fits, with space above and below. The selected image has no descender glyph, but
the lower inset provides baseline clearance; this fixed source does not attempt
to guess bounds for arbitrary custom pictures.

Two front/back capture iterations used isolated temporary review storage; byte
comparison confirmed reveal did not change retained schedules. Final visual build
passed in 13.56 s, 10 Ninja steps, no reconfigure; the first build/capture iteration
was not separately timed. Five-group product aggregate is running. Captures:
`build-ninja-debug/flashcards-printed-word/production-front.bmp` and
`production-back.bmp` at the Draxul root. All edits remain uncommitted/unpushed.

## Completed validation

`py do.py test debug --flashcards --label flashcards`: 5/5 passed, 194.53 s
(runner 194.66 s). Build before the aggregate passed in 13.25 s, eight staging/check
steps, no reconfigure. All model, generator, regular review, native cue and private
audio groups passed. No new source tests were added for this reversible visual
change; isolated actual front/back captures validate the changed cue itself.
Standard same-cache `py do.py smoke --skip-build` passed, outer 27.79 s. Final
isolated `do.py run release --console -- --plugin dev.draxul.flashcards --smoke-test`
with logging passed: 13.34 s build, ten steps, no reconfigure; outer build/startup
15.48 s, startup alone not separately timed. Log confirms Vulkan swapchain and
Flashcards readiness. Earlier host failures remain historical open issues, not
claimed fixed by this visual slice. macOS and remote CI were not executed.
Source licence/provenance, README and root feature inventory are updated; final
front/back are inspected. No journal edit, live review-state rewrite, commit or
push occurred. Existing human-audio changes are preserved.

2026-10-04: the user explicitly authorized committing and pushing all pending
work. The photographic implementation and its existing validation are included
in that publication; the earlier no-publication instruction is superseded.
