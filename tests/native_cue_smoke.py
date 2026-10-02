"""Packaged native-cue guide, answer gating and actual durable grading."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time

from render_smoke import capture

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("embed_deck", ROOT / "tools/embed_deck.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    deck = embed.build_deck(args.source, ROOT / "assets/artwork.json")
    native = [c for c in deck["cards"] if c["cue"] != "picture"]
    if not native:
        print("Selected source has no native cues; model/generator fixture coverage remains available.")
        return
    with tempfile.TemporaryDirectory(prefix="draxul-native-cue-") as temporary:
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        if sys.platform != "win32":
            capture(args.exe, args.out, "guide", env)
            print("Guide rendered; native input lifecycle automation is Windows-only.")
            return
        env.update(APPDATA=temporary, LOCALAPPDATA=temporary)
        directory = Path(temporary) / "draxul/plugin-config/dev.draxul.flashcards/state"
        state_path = directory / "recall-v1.json"
        guide_path = directory / "cue-guide-v1.json"

        def queued(name):
            return (args.out / (name + ".log")).read_text(errors="replace").count("pronunciation queued after reveal")

        capture(args.exe, args.out, "guide", env, [ord("R"), ord("1"), ord("2")])
        if state_path.exists() or guide_path.exists() or queued("guide"):
            raise RuntimeError("Studying the guide invented recall, audio or acknowledgment")
        capture(args.exe, args.out, "guide-finished", env,
                [13] * len(native) + [ord("R"), ord("2")])
        if not guide_path.exists() or json.loads(guide_path.read_text()).get("acknowledged") is not True:
            raise RuntimeError("Completing all guide pages did not persist its separate preference")
        if state_path.exists() or queued("guide-finished"):
            raise RuntimeError("Closing the guide graded or played an unrevealed card")

        baseline = {"stage": 1, "due": int(time.time()) + 86400,
                    "last_reviewed": int(time.time()) - 20, "reviews": 1,
                    "remembered": 1, "forgotten": 0, "assisted": 0}
        for card in native:
            name = card["cue"]
            others = {direction + ":" + c["id"]: dict(baseline)
                      for c in deck["cards"] if c["id"] != card["id"]
                      for direction in ("recognition", "production")}
            state_path.write_text(json.dumps({"schema_version": 1, "cards": others}))
            saved = state_path.read_bytes()
            capture(args.exe, args.out, name + "-recognition-back", env, [32, ord("R")])
            if queued(name + "-recognition-back") != 2 or state_path.read_bytes() != saved:
                raise RuntimeError("Native recognition did not reveal/replay without changing grades")
            front = capture(args.exe, args.out, name + "-production-front", env, [32, ord("2")])
            state = json.loads(state_path.read_text())["cards"]
            rkey, pkey = "recognition:" + card["id"], "production:" + card["id"]
            if len(state) != len(others) + 1 or rkey not in state or pkey in state:
                raise RuntimeError("Native recognition grading did not retain independent directions")
            recognition = state[rkey]
            if recognition["remembered"] != 1 or any(state[k] != value for k, value in others.items()):
                raise RuntimeError("Native grading damaged other history")
            saved = state_path.read_bytes()
            back = capture(args.exe, args.out, name + "-production-back", env, [32, ord("R")])
            if state_path.read_bytes() != saved or queued(name + "-production-back") != 2:
                raise RuntimeError("Native production reveal/replay wrote a grade")
            if sum(a != b for a, b in zip(front, back)) < 5000:
                raise RuntimeError("Native production front did not change on reveal")
            capture(args.exe, args.out, name + "-graded", env, [32, ord("1")])
            state = json.loads(state_path.read_text())["cards"]
            if state.get(rkey) != recognition or state.get(pkey, {}).get("forgotten") != 1:
                raise RuntimeError("Native production grade changed recognition or failed to save")
            if any(state[k] != value for k, value in others.items()):
                raise RuntimeError("Native production grade rewrote other schedules")
        print("Native cue guide, media/audio, both faces and independent durable grades passed.")


if __name__ == "__main__":
    main()
