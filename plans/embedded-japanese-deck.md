# Embedded Japanese flashcards: first vertical slice

Product: `dev.draxul.flashcards`, proposed public repository `cmaughan/draxul-flashcards`.
Mount: `plugins/flashcards`, controlled by `DRAXUL_ENABLE_FLASHCARDS`.

## Outcome

Open a due card, see kana plus an image hint, flip to a meaning-bearing picture, explicitly grade recall,
close/rebuild/reopen, and retain the resulting due schedule. Runtime needs no journal
or network access. This is independent of the unfinished Personal Assistant feature.

## Boundaries and files

- Root CMake: optional mounted product registration; test-scope registration and
  `do.py --flashcards`; canonical feature inventory. Preserve existing dirty work.
- Product `tools/embed_deck.py`: bounded, strict schema-1 journal validation, stable
  IDs, duplicate/conflict rejection, field whitelist (no English meaning in runtime), deterministic generated C++
  header in the build tree. An always-run build check replaces the header only when
  packaged content changes. Configurable `DRAXUL_FLASHCARDS_VOCABULARY_SOURCE`;
  public-safe fixture by default. Source errors stop the build, never silently use
  an old snapshot. No personal path or conversation/review fields in the payload.
- Product `src/review.*`: review state by stable ID, due selection, serialized
  schema-1 state, callback-based atomic commit through SDK plugin-scope storage.
  Unseen cards due now; correct intervals 1/3/7/14/30 days, then repeating 30;
  forgotten resets stage and retries after 10 minutes. One grade per revealed card.
  Save before advance; retain removed IDs. Invalid state blocks grading and is not
  overwritten. Re-read state before grading to protect multiple panes from stale
  overwrites. Conversational exposure scheduling remains untouched.
- Product `src/flashcards_plugin.cpp`: C ABI lifecycle, NanoVG frame adapter on
  Vulkan/Metal, product UI and hit testing, Space flip, 1 forgotten, 2 recalled.
  Front shows kana only with an optional image hint. Back shows the image, kana,
  romanization and creator/license attribution; neither face draws translations.
  Hinted grades retry after 10 minutes without expanding the recall interval. Due/empty/completion/error states;
  embedded snapshot fingerprint and checked-at-build description without paths.
- Product `assets/`: vetted cached hints by ID and Japanese font, with source,
  license and attribution manifests. No build/runtime downloads; explicit missing
  image state. Public fixtures are illustrative and not copied from the journal.
- Product tests: generator build boundary, review lifecycle with controlled clock
  and durable storage, input transitions, corruption and concurrent-pane handling.

## Acceptance and validation

1. Source changes after configure are incorporated by the next enabled build;
   unchanged content does not rewrite generated headers. Missing, conflicted,
   unsupported, oversized, duplicate-ID, and malformed sources fail clearly.
2. Private context, exposure schedule, and absolute paths are absent from embedded
   output. Personal artifacts stay in ignored build directories and are never
   published. Fixture builds are suitable for public CI.
3. Kana preserves hiragana/katakana. Front has kana and an optional hint; back has a vetted picture and kana, with no
   English translation on either face; back offers explicit grades. Unsupported assets fail gracefully.
4. Tests cover due boundaries, grades/repeat events, failed commits, reopened state,
   newly added/removed/restored IDs, corrupt schemas and stale pane state.
5. Run core + flashcards aggregate and same-cache startup smoke, then render/capture
   and inspect the visible host. Windows is locally available; retain shared Metal
   path and document unavailable macOS verification. Record validation cost.
6. Record exact enabled-build and launch steps only after the build succeeds.

## Setup gate and risks

The user chose a public `draxul-flashcards` repository. Local implementation precedes
publication. A real submodule requires an initial product commit and deliberate core
gitlink adoption; do not commit unrelated or personal files. Commit authorization
remains pending. Validate Japanese font coverage, relevant image licensing, front
answer leakage, state persistence across rebuilt packages, and multi-pane writes.
All remaining gates stay in the product pending card until completed.

## Superseding user choice

Translation-free recognition is the first direction. See
[research and adaptations](fluent-forever-research.md). Meanings/pronunciation prose
are validated source data but are excluded from the embedded runtime payload.
Recognition progress uses kind-qualified keys; future production must be independent.
