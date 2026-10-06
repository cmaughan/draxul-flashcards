# Human pronunciation coverage, refreshed 2026-10-06

The user prefers natural human recordings, ideally 2–3 distinct native Japanese
speakers per entry where suitable clips are available. This preference applies
to future media curation. No new words or purchases are authorized by it.

| Entry | Cached human clips | Speaker evidence | Current playback |
|---|---:|---|---|
| ことば (kotoba) | 1 | Tofugu archive voice; publisher says its archive voices are native Japanese, but does not assign a name/accent to this file | Human |
| エージェント (ejento) | 0 | No suitable reusable exact clip curated | Clearly labelled Mei synthetic fallback |
| あさ (asa) | 2 | Tofugu archive voice as above; CKali named by Lingua Libre, native status/gender unverified | Human, Another speaker cycles |
| それ (sore) | 0 | Commons Ja-sore identifies a non-native speaker; excluded from native learning coverage | Clearly labelled Mei synthetic fallback |
| が (ga) | 0 | No reusable recording verified for this subject-marker card; no sentence audio was chopped | Clearly labelled Mei synthetic fallback |
| いいね (ii ne) | 0 | Complete friendly approval phrase required; no concatenated syllables or split entries | Clearly labelled Mei synthetic fallback |
| しゃしん (shashin) | 1 | Tofugu archive voice; native Japanese stated by publisher, file-level identity unspecified | Human |
| リスト (risuto) | 0 | No exact suitable reusable human clip found in the checked archive/Commons sources | Pronunciation unavailable, no silent synthesis |
| きょう (kyou) | 1 | Tofugu archive voice; native Japanese stated by publisher, file-level identity unspecified | Human |

The 2–3 native-speaker target is **not yet met** for any entry. Four entries now
have human coverage, with one publisher-supported native voice per entry and an
additional named human voice of unverified native status for あさ. This report
does not infer gender, voice identity, native status, pitch accuracy or user recall.

## Sources, licences and edits

[Tofugu/WaniKani publisher archive](https://github.com/tofugu/japanese-vocabulary-pronunciation-audio/tree/9725e0e7d628ab616e8b14e126d3daa33eba8d36)
is pinned to commit `9725e0e7d628ab616e8b14e126d3daa33eba8d36`.
Its README identifies two native Japanese archive voices (Tokyo professional and
Kansai amateur); the per-file assignment is unspecified. Exact complete word
Ogg files are decoded to mono 48 kHz PCM16, without trimming, gain, pitch, speed
or concatenation edits. Credit Tofugu and WaniKani. Files and any adaptations
remain CC BY-SA 4.0; full licence is `assets/TOFUGU-AUDIO-LICENSE.txt`.

[CKali's Lingua Libre recording](https://commons.wikimedia.org/wiki/File:LL-Q5287_(jpn)-CKali-%E3%81%82%E3%81%95.wav)
states Japanese transcription あさ, speaker CKali and CC0. The original 48 kHz
mono WAV is unchanged. The page does not establish native status or gender.

| File | Duration | Peak amplitude | Clipped PCM samples | Source SHA-256 |
|---|---:|---:|---:|---|
| kotoba-tofugu.wav | 0.652 s | 0.133 | 0 | `972d8b7e174ef2164a4b0ae1bccaa31d69ce742493ef1ece0c2038c6fa02930a` (original Ogg) |
| asa-tofugu.wav | 0.605 s | 0.369 | 0 | `b3568225dc8619d0adf8af9eb20bfab5832121d53a1361c532c24c395de85d11` (original Ogg) |
| asa-ckali.wav | 0.968 s | 0.388 | 0 | See unchanged WAV checksum in `assets/artwork.json` |

Source transcripts/spellings, licences, file hashes, decodability, channels,
sample rate, duration and signal bounds were inspected. **This agent session
has no audio perception capability**; no subjective listening, semantic sound
verification or pronunciation quality assessment is claimed. Levels vary; the
shorter Tofugu word recordings and quiet kotoba clip especially need human listening
review. Packaged dummy-driver tests establish queue/replay behavior only.

## Candidates and remaining work

[Commons Ja-sore](https://commons.wikimedia.org/wiki/File:Ja-sore.ogg) is CC0 but
explicitly describes a non-native speaker. It is not installed as native coverage.
Lingua Libre/Commons file lists and the licensed Tofugu archive were checked;
this is not a claim that no other reusable recordings exist elsewhere.

Forvo offers listening candidates for
[それ](https://forvo.com/word/%E3%81%9D%E3%82%8C/),
[が](https://forvo.com/word/%E3%81%8C/), and
[いいね](https://forvo.com/word/%E3%81%84%E3%81%84%E3%81%AD/).
Speaker profiles supply names and locations, which alone do not prove native
status. Its [terms](https://forvo.com/terms-and-conditions/) restrict use to
provided interfaces and personal/noncommercial purposes; no public redistributable
licence was verified. Nothing was extracted from playback internals or cached
from Forvo. Authorized personal downloads can be configured with the external
private manifest described in the README. Tatoeba sentence audio was not chopped
to fill these isolated-word/particle gaps.

Track further exact recordings, verified permissions, native-speaker coverage and
human listening review in [the human audio card](../kanban/pending/04%20human-pronunciation-and-speaker-cycling%20-feature.md).

## Refresh evidence, 2026-10-06

The authenticated GitHub tree of the same pinned Tofugu commit contains exact
complete photograph and today recordings. Downloaded original Oggs into the
ignored curation directory, decoded to mono 48 kHz PCM16 without trimming,
gain/pitch/speed changes or concatenation. Public WAV hashes and original URLs
are in artwork.json; the existing full publisher licence applies.

| New cache | Duration | Peak amplitude | Clipped PCM samples |
|---|---:|---:|---:|
| shashin-tofugu.wav | 0.849 s | 0.189 | 0 |
| kyou-tofugu.wav | 0.509 s | 0.305 | 0 |

All nine current entries have vetted visual cues. Eight have selected cached
pronunciation; list has none. Human media covers four entries (five clips,
including the previously named CKali voice with unverified native status).
Five entries still lack verified human recordings: agent, near-listener that,
subject-marker particle, the complete approval phrase, and list. The licensed
archive has no exact entries for those spellings; the checked Commons search
provided no new suitable exact list recording. This is bounded curation evidence,
not a claim that recordings do not exist elsewhere. No private media was copied
into the public package. No new native status, speaker identity, subjective
listening, pronunciation quality or recall was inferred.

Remaining coverage/listening work stays in the human-audio card. The current
refresh is tracked separately as upcoming-cue-column-and-current-deck.
