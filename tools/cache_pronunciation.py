"""Explicit offline media curation; never invoked by a normal build/runtime.

Install pyopenjtalk-plus==0.4.1.post9 into a separate curation environment first.
The wheel includes its dictionary/Mei voice. No personal journal is read.
"""
import argparse
import re
import wave
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kana", required=True)
    parser.add_argument("--name", required=True, help="Public media filename stem")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9_-]{1,96}", args.name):
        parser.error("Name must be a safe filename stem")
    if not args.kana or len(args.kana) > 64 or not all(
        0x3041 <= ord(c) <= 0x309f or 0x30a1 <= ord(c) <= 0x30ff or c in " ・" for c in args.kana
    ):
        parser.error("Expected bounded hiragana/katakana")
    import numpy as np
    import pyopenjtalk
    samples, rate = pyopenjtalk.tts(args.kana)
    pcm = np.clip(samples, -32768, 32767).astype("<i2")
    if len(pcm) / rate > 10:
        raise ValueError("Pronunciation exceeds ten seconds")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.output_dir / (args.name + ".wav")), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm.tobytes())
    print("Cached synthetic pronunciation. Add its mapping/credit to artwork.json before use.")


if __name__ == "__main__":
    main()
