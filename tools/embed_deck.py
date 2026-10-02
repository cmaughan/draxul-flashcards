"""Read-only journal validation and deterministic, translation-free embedding."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

MAX_BYTES = 2 * 1024 * 1024
MAX_WORDS = 2000
ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,95}$")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def read_json(path):
    # Reject the common Dropbox conflicted-copy naming convention, including
    # a sibling copy of this source. Check on every build even with old mtimes.
    if "conflicted copy" in path.name.lower() or any(
        "conflicted copy" in p.name.lower() and p.suffix == path.suffix
        and p.name.lower().startswith(path.stem.lower())
        for p in path.parent.iterdir()
    ):
        raise ValueError("conflicted source; reconcile its copies first")
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("source exceeds 2 MiB")
    value = json.loads(data.decode("utf-8-sig"), object_pairs_hook=unique_object,
                       parse_constant=lambda _: (_ for _ in ()).throw(ValueError("non-finite JSON")))
    def check_depth(item, depth=0):
        if depth > 24:
            raise ValueError("JSON nesting exceeds 24 levels")
        if isinstance(item, dict):
            for child in item.values():
                check_depth(child, depth + 1)
        elif isinstance(item, list):
            for child in item:
                check_depth(child, depth + 1)
    check_depth(value)
    return value


def text_field(word, name, limit, required=False):
    value = word.get(name, "")
    if not isinstance(value, str) or len(value.encode("utf-8")) > limit:
        raise ValueError(f"invalid or oversized {name}")
    if required and not value.strip():
        raise ValueError(f"missing {name}")
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError(f"control character in {name}")
    return value


def build_deck(source, artwork):
    journal = read_json(source)
    if not isinstance(journal, dict) or type(journal.get("schema_version")) is not int or journal["schema_version"] != 1:
        raise ValueError("unsupported vocabulary schema; expected 1")
    allowed_top = {"schema_version", "timezone", "minimum_interval_hours", "last_introduced_at_utc", "next_eligible_at_utc", "words"}
    if journal.keys() - allowed_top:
        raise ValueError("unknown vocabulary schema field")
    words = journal.get("words")
    if not isinstance(words, list) or len(words) > MAX_WORDS:
        raise ValueError("words must be an array of at most 2000 entries")
    art = read_json(artwork)
    if not isinstance(art, dict) or art.get("schema_version") != 3 or art.keys() != {"schema_version", "assets", "audio", "by_kana", "by_id", "audio_by_kana", "cues", "cue_by_kana"}:
        raise ValueError("unsupported artwork schema")
    if not all(isinstance(art[k], dict) for k in ("assets", "audio", "by_kana", "by_id", "audio_by_kana", "cues", "cue_by_kana")):
        raise ValueError("invalid artwork mappings")
    for cue in art["cues"].values():
        if not isinstance(cue, dict) or cue.keys() != {"kind", "subject", "attribution"}:
            raise ValueError("invalid native cue metadata")
        text_field(cue, "kind", 64, True)
        text_field(cue, "subject", 192)
        if cue["kind"] not in {"listener-reference", "subject-marker", "approval-reaction"}:
            raise ValueError("unsupported native cue")
        text_field(cue, "attribution", 512, True)
        if cue["subject"] != ("それ" if cue["kind"] == "subject-marker" else ""):
            raise ValueError("invalid native cue subject")
    if any(not isinstance(key, str) or not isinstance(val, str) or val not in art["cues"]
           for key, val in art["cue_by_kana"].items()):
        raise ValueError("cue mapping refers to an unknown cue")
    for asset in list(art["assets"].values()) + list(art["audio"].values()):
        if not isinstance(asset, dict) or asset.keys() != {"file", "source", "license", "attribution", "changes"}:
            raise ValueError("invalid artwork metadata")
        for field in asset:
            text_field(asset, field, 512, True)
        if not re.fullmatch(r"[a-z0-9_-]+\.(png|jpg|jpeg|wav)", asset["file"]) or not asset["source"].startswith("https://") or asset["license"] not in {"CC-BY-4.0", "CC-BY-SA-4.0", "CC0-1.0", "CC-BY-3.0"}:
            raise ValueError("media needs a vetted file, source and supported bundling license")
    for mapping, assets in ((art["audio_by_kana"], art["audio"]),):
        if any(not isinstance(key, str) or not isinstance(val, str) or val not in assets for key, val in mapping.items()):
            raise ValueError("audio mapping refers to an unknown asset")
    for mapping in (art["by_id"], art["by_kana"]):
        if any(not isinstance(key, str) or not isinstance(val, str) or val not in art["assets"] for key, val in mapping.items()):
            raise ValueError("artwork mapping refers to an unknown asset")
    allowed_word = {"id", "kana", "romaji", "meaning", "pronunciation_guide", "review", "introduced_at_utc", "introduction_source", "conversation_context", "example", "recognition_notes", "pronunciation_observations", "review_notes", "source"}
    cards, ids = [], set()
    for word in words:
        if not isinstance(word, dict) or word.keys() - allowed_word:
            raise ValueError("invalid word schema")
        word_id = text_field(word, "id", 96, True)
        if not ID.fullmatch(word_id) or word_id in ids:
            raise ValueError("invalid or duplicate stable word ID")
        ids.add(word_id)
        kana = text_field(word, "kana", 192, True)
        if not all(0x3041 <= ord(c) <= 0x3096 or 0x3099 <= ord(c) <= 0x309f or 0x30a1 <= ord(c) <= 0x30ff or c in " ・" for c in kana):
            raise ValueError("kana must contain hiragana/katakana")
        text_field(word, "meaning", 512, True)  # Sense information stays private.
        text_field(word, "pronunciation_guide", 2048)
        romaji = text_field(word, "romaji", 256)
        asset_id = art["by_id"].get(word_id, art["by_kana"].get(kana, ""))
        asset = art["assets"].get(asset_id, {})
        cue = {} if asset_id else art["cues"].get(art["cue_by_kana"].get(kana, ""), {})
        cue_kana = {"listener-reference": "それ", "subject-marker": "が", "approval-reaction": "いいね"}
        if cue and kana != cue_kana[cue["kind"]]:
            raise ValueError("native cue does not match its spelling")
        audio = art["audio"].get(art["audio_by_kana"].get(kana, ""), {})
        cards.append({"id": word_id, "kana": kana, "romaji": romaji,
                      "image": asset.get("file", ""), "attribution": cue.get("attribution", asset.get("attribution", "")),
                      "cue": cue.get("kind", "picture"), "cue_subject": cue.get("subject", ""),
                      "audio": audio.get("file", ""), "audio_attribution": audio.get("attribution", "")})
    active_kana = {card["kana"] for card in cards}
    if any(card["cue_subject"] and card["cue_subject"] not in active_kana for card in cards):
        raise ValueError("native cue depends on vocabulary outside this deck")
    return {"schema_version": 3, "kind": "bidirectional", "cards": sorted(cards, key=lambda c: c["id"])}


def generate(source, artwork, output):
    payload = json.dumps(build_deck(source, artwork), ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if len(payload) > 1024 * 1024:
        raise ValueError("embedded deck exceeds 1 MiB")
    digest = hashlib.sha256(payload).hexdigest()
    literals = ["\"" + "".join(f"\\{b:03o}" for b in payload[i:i+100]) + "\"" for i in range(0, len(payload), 100)]
    header = ("// Generated in the build tree. Never publish personal deck binaries.\n#pragma once\n#include <string_view>\nnamespace flashcards::embedded {\ninline constexpr std::string_view deck =\n" + "\n".join(literals) + f";\ninline constexpr std::string_view fingerprint = \"{digest}\";\n}}\n").encode()
    if output.exists() and output.read_bytes() == header:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=output.parent, prefix=".deck-")
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(header)
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--artwork", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        generate(args.source, args.artwork, args.output)
    except (OSError, ValueError, RecursionError) as error:
        # Avoid echoing personal file contents or absolute paths into build logs.
        message = "source is missing/unreadable" if isinstance(error, OSError) else "malformed JSON" if isinstance(error, json.JSONDecodeError) else str(error)
        print(f"Flashcards deck check failed: {message}", file=sys.stderr)
        sys.exit(1)
