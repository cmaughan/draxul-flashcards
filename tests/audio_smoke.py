"""Actual offline private clip cycling, missing-file behavior and state isolation.

Fixture duplicates exercise selection only, not distinct-speaker curation claims.
The checked source manifest supplies the genuine packaged speaker provenance.
"""
import argparse, importlib.util, json, os, shutil, sys, tempfile
from pathlib import Path
from render_smoke import capture

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("embed_deck",ROOT/"tools/embed_deck.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--source",type=Path,required=True)
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    deck=embed.build_deck(args.source,ROOT/"assets/artwork.json")
    if not deck["cards"] or not deck["cards"][0]["audio"] or sys.platform != "win32":
        print("Private input harness requires Windows and a nonempty audio deck."); return
    card=deck["cards"][0]
    with tempfile.TemporaryDirectory(prefix="draxul-audio-test-") as temporary:
        base=Path(temporary); private=base/"private-media"; private.mkdir()
        env=dict(os.environ,APPDATA=temporary,LOCALAPPDATA=temporary,SDL_AUDIODRIVER="dummy")
        state_dir=base/"draxul/plugin-config/dev.draxul.flashcards/state"; state_dir.mkdir(parents=True)
        (state_dir/"cue-guide-v1.json").write_text('{"schema_version":1,"acknowledged":true}')
        state=state_dir/"recall-v1.json"
        clips=[]
        for index in range(2):
            source=card["audio"][index % len(card["audio"])]
            name="private-speaker-"+str(index)+".wav"
            shutil.copyfile(ROOT/"assets"/source["file"],private/name)
            clips.append(dict(file=name,attribution="Private selector fixture",speaker="Fixture "+str(index),
                synthetic=False,source="https://example.com/fixture",license="test-fixture-only",
                permission="personal-use-authorized",changes="Selection fixture copy",quality="Not a speaker/quality assessment"))
        manifest=private/"manifest.json"
        manifest.write_text(json.dumps(dict(schema_version=1,entries={card["id"]:dict(kana=card["kana"],clips=clips)})),encoding="utf8")
        config={"audio_directory":str(private)}
        capture(args.exe,args.out,"private-front",env,[ord("R"),ord("N"),ord("2")],config=config)
        if state.exists() or "pronunciation queued" in (args.out/"private-front.log").read_text():
            raise RuntimeError("Front speaker input leaked audio or a grade")
        capture(args.exe,args.out,"private-cycle",env,[32,ord("N"),ord("R"),ord("N")],config=config)
        log=(args.out/"private-cycle.log").read_text()
        positions=[line.split("clip=")[1].split()[0] for line in log.splitlines() if "pronunciation clip=" in line]
        if positions != ["0","1","1","0"] or state.exists() or log.count("queued after reveal") != 4:
            raise RuntimeError("Replay/cycle changed selection incorrectly or invented review progress")
        (private/clips[0]["file"]).unlink()
        capture(args.exe,args.out,"private-missing",env,[32,ord("R"),ord("N"),ord("2")],config=config)
        log=(args.out/"private-missing.log").read_text()
        if log.count("cached pronunciation unavailable") != 2 or log.count("queued after reveal") != 1:
            raise RuntimeError("Missing human clip silently fell back instead of requiring explicit cycling")
        stored=json.loads(state.read_text())["cards"]
        if len(stored)!=1 or stored["recognition:"+card["id"]]["remembered"]!=1:
            raise RuntimeError("Missing pronunciation blocked or damaged an explicit visual grade")
        package=args.exe.parent/"plugins/dev.draxul.flashcards"
        if not package.exists(): raise RuntimeError("Packaged Flashcards directory missing")
        if any(list(package.rglob(c["file"])) for c in clips):
            raise RuntimeError("Private media leaked into the package")
        print("Private offline cycling/replay, front gates, missing clip, explicit grade and package exclusion passed.")

if __name__=="__main__": main()
