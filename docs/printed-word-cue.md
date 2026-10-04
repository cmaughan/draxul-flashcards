# Printed-word photographic cue

User-approved replacement for the ことば default, 2026-10-02. A conversation
photograph suggested conversation rather than a single word. The replacement
shows a printed book page with one actual printed word emphasized; no target
kana, romanization or English translation is added to the production front.

- Photographer: Kasharp; own work dated 2015-02-27.
- Source/licence evidence: [Words on a Page](https://commons.wikimedia.org/wiki/File:Words_on_a_Page.png).
- Original: [2448 × 3264 PNG](https://upload.wikimedia.org/wikipedia/commons/e/e0/Words_on_a_Page.png),
  cached unchanged as `assets/printed-word.png`.
- SHA-256: `5fcf5659d4a0ea2f29b499cfc992a03495d2d6515588bced362074e565176f53`.
- Licence: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/);
  full text is already packaged in `assets/TOFUGU-AUDIO-LICENSE.txt` (the same
  standard licence). Credit and modification notice appear on the card back.

The renderer rotates/crops source pixels around the printed word “well”, keeps
that region at full clarity, washes the surroundings with a translucent paper
colour and draws a gold outline. It never types replacement text onto the photo.
The displayed photographic adaptation is offered under CC BY-SA 4.0. Surrounding
text remains photographic context; it is subdued to make the selected word the
visual referent. The word shown is an example of a written word, not a translation
of the Japanese target. Source file pixels are not blurred, sharpened or edited.

This changes the default media mapping only. Custom stable-ID images retain their
own display, pronunciation selections stay intact, and both recognition and
production progress continue using the existing keys. QA uses temporary storage
and does not grade or rewrite real user schedules.

Rebuilt production front and back were inspected at actual 900x760 size after
correcting the initially clipped outline. Both show the complete word with upper
and lower padding. The product aggregate and Debug/Release startup gates passed;
costs and preserved limitations are in [validation.md](validation.md). This visual
replacement and the ongoing human-audio work remain uncommitted and unpushed.

2026-10-04: the user authorized publishing all pending work. The source photograph,
display adaptation and provenance are included with the Flashcards audio changes.
