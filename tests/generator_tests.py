import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("embed_deck", ROOT / "tools/embed_deck.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


class GeneratorBoundary(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "vocabulary.json"
        self.art = ROOT / "assets/artwork.json"
        self.output = self.root / "generated/embedded_deck.h"
        self.doc = json.loads((ROOT / "tests/fixtures/vocabulary.json").read_text(encoding="utf-8"))
        self.write()

    def tearDown(self):
        self.temporary.cleanup()

    def write(self):
        self.source.write_text(json.dumps(self.doc, ensure_ascii=False), encoding="utf-8")

    def test_privacy_script_preservation_and_incremental_refresh(self):
        self.doc["words"][0].update(conversation_context="PRIVATE_CONTEXT", review={"next_review_at_utc": "PRIVATE_TIME"}, pronunciation_guide="PRIVATE_GUIDE")
        self.write()
        original = self.source.read_bytes()
        deck = embed.build_deck(self.source, self.art)
        payload = json.dumps(deck, ensure_ascii=False)
        for private in ("PRIVATE_CONTEXT", "PRIVATE_TIME", "PRIVATE_GUIDE", "meaning", str(self.root)):
            self.assertNotIn(private, payload)
        self.assertEqual(set(deck["cards"][0]), {"id", "kana", "romaji", "image", "attribution", "audio", "audio_attribution", "cue", "cue_subject"})
        self.assertEqual(deck["schema_version"], 3)
        self.assertEqual(deck["kind"], "bidirectional")
        self.assertTrue(all(c["audio"].endswith(".wav") for c in deck["cards"]))
        self.assertEqual({c["kana"] for c in deck["cards"]}, {"りんご", "ロボット", "それ", "が", "いいね"})
        self.assertTrue(embed.generate(self.source, self.art, self.output))
        stamp = self.output.stat().st_mtime_ns
        self.assertFalse(embed.generate(self.source, self.art, self.output))
        self.assertEqual(stamp, self.output.stat().st_mtime_ns)
        self.assertEqual(original, self.source.read_bytes())
        self.doc["words"].append({"id": "added", "kana": "あ", "meaning": "new fixture", "romaji": "a"})
        self.write()
        self.assertTrue(embed.generate(self.source, self.art, self.output))
        self.assertEqual(len(embed.build_deck(self.source, self.art)["cards"]), 6)

    def test_native_cues_have_media_and_only_active_dependencies(self):
        import wave
        deck = embed.build_deck(self.source, self.art)
        by_kana = {c["kana"]: c for c in deck["cards"]}
        for kana, cue in (("それ", "listener-reference"), ("が", "subject-marker"), ("いいね", "approval-reaction")):
            card = by_kana[kana]
            self.assertEqual(card["cue"], cue)
            self.assertEqual(card["image"], "")
            self.assertTrue(card["attribution"])
            with wave.open(str(ROOT / "assets" / card["audio"]), "rb") as audio:
                self.assertEqual(audio.getnchannels(), 1)
                self.assertEqual(audio.getsampwidth(), 2)
                self.assertEqual(audio.getframerate(), 48000)
                self.assertGreater(audio.getnframes(), 1000)
        self.assertEqual(by_kana["が"]["cue_subject"], "それ")
        self.doc["words"] = [w for w in self.doc["words"] if w["kana"] != "それ"]
        self.write()
        with self.assertRaisesRegex(ValueError, "outside this deck"):
            embed.build_deck(self.source, self.art)

    def test_invalid_sources_preserve_previous_output(self):
        embed.generate(self.source, self.art, self.output)
        original = self.output.read_bytes()
        invalid = []
        duplicate = copy.deepcopy(self.doc)
        duplicate["words"].append(duplicate["words"][0])
        invalid.append(duplicate)
        for replacement in ({"schema_version": 2}, {"schema_version": True}, {"words": [] , "unknown": True}):
            doc = copy.deepcopy(self.doc)
            doc.update(replacement)
            invalid.append(doc)
        for field, value in (("kana", "apple"), ("meaning", ""), ("id", "../bad"), ("romaji", "a\n"), ("kana", "ア" * 1000)):
            doc = copy.deepcopy(self.doc)
            doc["words"][0][field] = value
            invalid.append(doc)
        for doc in invalid:
            self.source.write_text(json.dumps(doc), encoding="utf-8")
            with self.assertRaises(ValueError):
                embed.generate(self.source, self.art, self.output)
            self.assertEqual(original, self.output.read_bytes())
        for content in ('{"schema_version":1,"schema_version":1,"words":[]}', '{"words":NaN}', "{" , "[" * 200 + "]" * 200):
            self.source.write_text(content)
            with self.assertRaises(ValueError):
                embed.generate(self.source, self.art, self.output)
        self.source.write_bytes(b" " * (embed.MAX_BYTES + 1))
        with self.assertRaises(ValueError):
            embed.generate(self.source, self.art, self.output)
        self.source.unlink()
        with self.assertRaises(OSError):
            embed.generate(self.source, self.art, self.output)
        self.write()
        (self.root / "vocabulary (computer's conflicted copy).json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "conflicted"):
            embed.generate(self.source, self.art, self.output)

    def test_every_build_checks_without_reconfiguring(self):
        # Run the same always-check/BYPRODUCTS contract through a real Ninja
        # build, including changes made after the single configure step.
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ninja)
        def q(path): return str(path).replace("\\", "/")
        import sys
        script = f'''cmake_minimum_required(VERSION 3.25)
project(deck_boundary NONE)
add_custom_target(deck ALL
COMMAND "{q(Path(sys.executable))}" "{q(ROOT / 'tools/embed_deck.py')}"
--source "{q(self.source)}" --artwork "{q(self.art)}" --output "{q(self.output)}"
BYPRODUCTS "{q(self.output)}" VERBATIM)
'''
        (self.root / "CMakeLists.txt").write_text(script)
        build = self.root / "build"
        def run(*args): return subprocess.run(args, capture_output=True, text=True, timeout=20)
        result = run(cmake, "-S", str(self.root), "-B", str(build), "-G", "Ninja")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = run(cmake, "--build", str(build))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        before = self.output.read_bytes()
        self.doc["words"][0]["romaji"] = "updated"
        self.write()
        result = run(cmake, "--build", str(build))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotEqual(before, self.output.read_bytes())
        self.source.unlink()
        result = run(cmake, "--build", str(build))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source is missing/unreadable", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
