# Draxul Flashcards

A mounted native Draxul product plugin. First mode: translation-free Japanese
recognition. Front: kana. Back: kana, romanization, and a vetted picture association.
English translations and conversational journal details are never embedded.

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

Runtime uses the embedded snapshot offline, displaying its content fingerprint.
The journal can be unavailable at runtime. Rebuilds incorporate added words without
changing saved grades. Removed IDs retain review history if later restored.

**Never distribute binaries generated from a personal journal.** Public builds and
CI must select the fixture. Build directories, generated headers and local deck
overrides belong outside Git. A whitelist reduces payload exposure but the Japanese
word list itself is still personal data.

## Review

- Space / Flip: reveal the meaning-bearing image. Flipping never records success.
- H / Show hint: reveal the image before flipping; the grade is then assisted.
- After flipping: 1 / Forgot, 2 / Remembered. Mouse buttons provide the same actions.
- Missing image: clearly labeled, grading disabled, Skip advances without a grade.
- Invalid/unreadable review state: preserved, grading blocked. Repair the file or
  storage issue, then use Check due cards; the plugin never silently resets it.

Unseen cards are due immediately. Unaided remembered grades use intervals of 1, 3,
7, 14, 30, then repeating 30 days from grading time. Forgot resets the stage and
retries after 10 minutes. Hint-assisted remembered grades retry after 10 minutes
without expanding the unaided interval. These are simple documented design choices,
not a claim about Fluent Forever's proprietary algorithm or mastery.

Sessions stop after 20 grades to bound a large backlog. Check due cards starts
another batch. Forgotten cards wait for their retry time rather than reappearing
immediately. Caps Lock / Num Lock do not disable the controls.

State schema 1 uses `recognition:<stable-word-id>` keys. Each record stores due/last
review time (Unix UTC seconds), stage, and remembered/forgotten/assisted counters.
Each grade re-reads state under a nonblocking process file lock, rejects stale-card
grades and commits through the SDK's atomic plugin-scope JSON storage before advancing.
Close/rebuild/reopen keeps progress. Held keys and repeated mouse-down events cannot
grade subsequent cards. Separate future production cards need independent keys.

Storage key: `recall-v1`, under the host's plugin config path `state/recall-v1.json`.
Windows default: `%APPDATA%/draxul/plugin-config/dev.draxul.flashcards/`.
macOS default: `~/Library/Application Support/draxul/Plugin Config/dev.draxul.flashcards/`.
The conversational exposure schedule in the journal is never read into recall state
or changed by this product.

## Artwork and licensing

`assets/artwork.json` contains source, license, attribution and modification status
for each cached PNG, with kana defaults and stable-ID overrides. No normal build or
runtime network request occurs. `DRAXUL_FLASHCARDS_ARTWORK_MAP` selects an alternate
metadata/mapping file; referenced vetted PNGs must be present in the product asset
directory. Add ID overrides to choose a more personally meaningful picture without
changing the journal or losing recall history. New words lacking vetted art show
the missing-image state; curation is a deliberate update, not arbitrary web scraping.

Speech is a suggestive association for word/language; robot is an association for
the software-agent sense. Neither image is proof of unaided recall. Creator/license
attribution appears on the back without image titles translating the target.

Twemoji PNGs: Twitter and contributors, unmodified, CC BY 4.0; full license in
`assets/TWEMOJI-LICENSE.txt`, exact pinned source URLs in `artwork.json`.
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
source failures, due boundaries, reopen/deck changes, hints, failed saves, stale
grades, corrupt state, missing images and overlapping-writer lock exclusion.

The runtime render smoke uses the actual packaged module. See
[launch steps and validation evidence](docs/validation.md), including known host
failures and platform limits.
