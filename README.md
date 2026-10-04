# Draxul Flashcards

A mounted native Draxul product plugin for translation-free Japanese practice.
Each word has independent recognition and production schedules. Recognition shows
kana on the front; production shows a picture or native diagram association. Both backs show
the cue, kana, romanization and cached pronunciation. English translations
and conversational journal details are never embedded.

## Launch

Use `draxul --plugin dev.draxul.flashcards`, or the Flashcards new-tab entry in
Draxul's command palette. The manifest opts into `reuse_existing_tab`: repeated
tab launches focus the existing Flashcards tab with matching JSON configuration
in the same Session, even across Spaces. Explicit splits remain separate.

## Build source

`DRAXUL_ENABLE_FLASHCARDS` enables the mounted product; it defaults to ON.
`DRAXUL_FLASHCARDS_PLUGIN_DIR` defaults to `plugins/flashcards`. Unmounted enabled
products follow Draxul's normal skip/CI-required policy.

The CMake cache FILEPATH `DRAXUL_FLASHCARDS_VOCABULARY_SOURCE` selects a schema-1
journal. The default is the public fixture in `tests/fixtures/vocabulary.json`.
Set the environment variable of the same name when configuring to explicitly
replace that cached path; subsequent builds retain it. Use an absolute path on
your machine. No personal path is saved in product sources.

The deck check runs whenever the plugin is built, including ordinary incremental
builds with no configure step. It rejects missing/unreadable sources, Dropbox
conflicted copies, unsupported schemas, duplicate keys/IDs, excessive nesting,
oversized input/fields, non-kana spelling and malformed JSON. A failed check stops
the build; old installed binaries remain older snapshots until a successful build.
Generated headers change only when whitelisted card content changes.

Runtime uses the embedded snapshot offline.
The journal can be unavailable at runtime. Rebuilds incorporate added words without
changing saved grades. Removed IDs retain review history if later restored.

**Never distribute binaries generated from a personal journal.** Public builds and
CI must select the fixture. Build directories, generated headers and local deck
overrides belong outside Git. A whitelist reduces payload exposure but the Japanese
word list itself is still personal data.

## Review

- Space / click card / Reveal: animate the turn. Flipping never records success.
- After the turn finishes: pronunciation plays; R / Replay plays it again.
- N / Another speaker: cycle cached human recordings after reveal, where available.
- H / Help: English explanation of the visual conventions. New cue decks open
  this guide before reviews; completing its pages saves `cue-guide-v1` separately
  from recall. Studying help neither plays audio nor records grades.
- After revealing: 1 / Again, 2 / Remembered. Mouse buttons provide the same actions.
- Front-side hints, audio and grades are unavailable, including during the turn.
- Missing audio/device: an unavailable message appears; visual grading still works.
- Missing image: clearly labeled, grading disabled, Skip advances without a grade.
- Invalid/unreadable review state: preserved, grading blocked. Repair the file or
  storage issue, then use Check due cards; the plugin never silently resets it.

Unseen cards are due immediately. Unaided remembered grades use intervals of 1, 3,
7, 14, 30, then repeating 30 days from grading time. Again resets the stage and
retries after 10 minutes. These are simple documented design choices,
not a claim about Fluent Forever's proprietary algorithm or mastery.

Sessions stop after 20 grades to bound a large backlog. Check due cards starts
another batch. Forgotten cards wait for their retry time rather than reappearing
immediately. Caps Lock / Num Lock do not disable the controls.

State schema 1 uses `recognition:<stable-word-id>` and `production:<stable-word-id>`
keys. Existing recognition records retain their dates and counters; new production
cards start unseen. Each record stores due/last
review time (Unix UTC seconds), stage, and remembered/forgotten/assisted counters.
Each grade re-reads state under a nonblocking process file lock, rejects stale-card
grades and commits through the SDK's atomic plugin-scope JSON storage before advancing.
Close/rebuild/reopen keeps progress. Held keys and repeated mouse-down events cannot
grade subsequent cards. Historical assisted counters are retained, but new grades
are explicit self-reports. Grading one direction does not change
the other. The generated deck uses schema 4, with one media record per word expanded
into both directions by the review model.

Storage key: `recall-v1`, under the host's plugin config path `state/recall-v1.json`.
Windows default: `%APPDATA%/draxul/plugin-config/dev.draxul.flashcards/`.
macOS default: `~/Library/Application Support/draxul/Plugin Config/dev.draxul.flashcards/`.
The conversational exposure schedule in the journal is never read into recall state
or changed by this product.

## Artwork and licensing

`assets/artwork.json` contains source, license, attribution and modification status
for each cached PNG/JPEG and WAV, with kana defaults and stable-ID image overrides. No normal build or
runtime network request occurs. `DRAXUL_FLASHCARDS_ARTWORK_MAP` selects an alternate
metadata/mapping file; referenced vetted media must be present in the product asset
directory. Add ID overrides to choose a more personally meaningful picture without
changing the journal or losing recall history. New words lacking vetted art show
the missing-image state; curation is a deliberate update, not arbitrary web scraping.

The printed-page photograph emphasizes one actual printed word: a gold outline
keeps it sharp while surrounding text is subdued. Neither the target kana nor
its English translation appears in the picture-first cue. A robot in a laptop
and task scene associates with a software agent. Abstract associations can be
ambiguous, so choose a picture meaningful to you. Morning shows a person waking
in bed beside a daylight window and a 07:00 alarm. Prefer cues that clearly express
the intended sense over attractive but ambiguous scenery. Creator/license attribution appears on the back without image titles
translating the target.

Native NanoVG diagrams teach それ as the speaker pointing beside the listener;
が as a chip immediately after the highlighted subject それ (blank on the
production front); and いいね as one friendly approval phrase expressed by a
thumbs-up reaction to completed work. The grammar fragment teaches the requested
subject-marker function, not "is" or a uniquely determined completion of a full
sentence. Its Japanese dependency must exist in the active deck. Normal reviews
remain translation-free; the optional English guide explains these associations.

Runtime plugin configuration also supports replacement images without rebuilding:

```json
{
  "image_directory": "D:/FlashcardPictures",
  "images": { "kotoba": "my-printed-page.jpg" }
}
```

The directory must be absolute. Override filenames accept lowercase letters,
digits, underscores and hyphens with PNG/JPG/JPEG extensions; they cannot contain
paths. Stable IDs keep recall schedules intact. Missing external files fall back
to a bundled file of the same name; invalid or unavailable pictures cannot be
graded. `flip_duration_ms` optionally sets 100–3000 ms (default 580 ms).

Cached media sources and licenses:

- [Words on a Page](https://commons.wikimedia.org/wiki/File:Words_on_a_Page.png):
  Kasharp, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/);
  original PNG unchanged, with a rotated display crop, subdued surround and gold
  word outline. The photographic display composition shares that licence.
  See [source and display provenance](docs/printed-word-cue.md).
- [Woman waking up](https://www.publicdomainpictures.net/en/view-image.php?image=293351&picture=woman-waking-up):
  Petr Kratochvil, CC0; unmodified 1280x1920 download, displayed with aspect fit
  beside a separately drawn daylight window and early alarm.
- [OpenMoji 17.0.0](https://github.com/hfg-gmuend/openmoji/tree/17.0.0):
  HfG Schwabisch Gmund and contributors, CC BY-SA 4.0; original 618px robot/apple
  PNGs, license in `assets/OPENMOJI-LICENSE.txt`. The separately drawn software-task
  composition is offered under the same CC BY-SA 4.0 license.
- Human pronunciation: complete publisher-released Tofugu/WaniKani recordings
  for ことば and あさ, plus CKali's Lingua Libre あさ. Sources, SHA-256,
  licence, edits, stated speaker and quality limits are in `assets/artwork.json`;
  see [coverage and curation evidence](docs/audio-coverage.md).
- Fallback pronunciation WAVs: synthesized using pyopenjtalk-plus 0.4.1.post9 and the
  [Mei voice](https://github.com/tsukumijima/pyopenjtalk-plus/blob/v0.4.1-post9/pyopenjtalk/htsvoice/LICENSE_mei_normal.htsvoice),
  MMDAgent / Nagoya Institute of Technology, CC BY 3.0; full voice license in
  `assets/MEI-VOICE-LICENSE.txt`. Mono 48 kHz PCM16 clips are cached and marked
  synthetic. They are pronunciation aids, not native recordings or a pitch-accent
  assessment. New unmapped words have no pronunciation until curated.

Prefer natural human audio going forward, ideally 2–3 distinct native Japanese
speakers per entry where suitable recordings are available. Human and synthetic
clips cannot share a cycling list. Missing selected human files show unavailable;
the app never silently switches to synthesis. Clip changes, replay and cycling
do not modify recognition/production schedules or the conversational journal.

`audio_directory` optionally selects an absolute private cache outside the plugin
package, containing a schema-1 `manifest.json` and WAV files. Runtime overrides
are selected by stable ID plus exact kana; they never enter generated headers or
the CMake asset-copy list. Keep the directory outside every public repository and
published package. Use only recordings actually authorized for that personal use.
Each clip requires `file`, `speaker`, `synthetic` (false), `attribution`, `source`,
`license`, `changes`, `quality` and `permission` (`personal-use-authorized`).
The manifest has `schema_version: 1` and `entries`, mapping each ID to `kana` and
`clips` (up to eight distinct speakers). Private clips replace that entry's entire
public list. A missing or invalid configured manifest fails initialization;
a missing WAV reports unavailable while visual review stays usable.

Forvo listening/download pages are candidates, not permission to scrape or publish
their media. This product has no Forvo extractor, paid API or network playback.

`tools/cache_pronunciation.py` is an optional curation tool that accepts an explicit
kana string and safe asset name. It requires a separately installed
`pyopenjtalk-plus==0.4.1.post9`; it never reads the journal and is not a build/runtime
dependency. Update `artwork.json` with source and attribution when adding a clip.

Noto Sans JP Regular: SIL OFL 1.1; full license in `assets/NOTO-OFL.txt`, pinned
source in `assets/font-source.json`. The packaged font covers kana and Latin text.

See [research and adaptations](plans/fluent-forever-research.md) and
[slice design](plans/embedded-japanese-deck.md). Windows uses Vulkan, macOS uses
Metal through Draxul's existing backend-neutral NanoVG plugin adapter.

## Validation

The product suite is included by `py do.py test debug --flashcards`; same-cache
startup gate: `py do.py smoke --skip-build`. On macOS use `python3`.
Model/generator tests use public fixtures, deterministic clocks and
failure-injected storage. Runtime captures use the selected compiled deck with
isolated review storage.
They cover incremental generation without configure, private-field exclusion,
source failures, due boundaries, reopen/deck changes, direction isolation, failed saves, stale
grades, corrupt state, missing images and overlapping-writer lock exclusion.

The runtime render smoke uses the actual packaged module, captures front/turn/back
in both directions and verifies reveal-only audio/replay through SDL's dummy audio
driver. It does not establish audible output or pronunciation quality. See
[launch steps and validation evidence](docs/validation.md), including known host
failures and platform limits.
