"""Actual packaged queue/answer faces at normal and narrow widths, isolated state."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
from render_smoke import capture

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('embed_deck', ROOT/'tools/embed_deck.py')
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


def strip(path, left, right, top, bottom):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H',data,28)[0] // 8
    stride = (w*bpp+3) & ~3
    return b''.join(data[offset+(abs(h)-1-y if h>0 else y)*stride+left*bpp:
                             offset+(abs(h)-1-y if h>0 else y)*stride+right*bpp]
                    for y in range(top,bottom))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--source',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    deck=embed.build_deck(args.source,ROOT/'assets/artwork.json')['cards']
    with tempfile.TemporaryDirectory(prefix='draxul-upcoming-') as temporary:
        env=dict(os.environ,SDL_AUDIODRIVER='dummy')
        if sys.platform!='win32':
            capture(args.exe,args.out,'queue-normal',env)
            capture(args.exe,args.out,'queue-narrow',env,dimensions=(620,720))
            print('Both layouts captured; native lifecycle input checks are Windows-only.')
            return
        env.update(APPDATA=temporary,LOCALAPPDATA=temporary)
        directory=Path(temporary)/'draxul/plugin-config/dev.draxul.flashcards/state'
        directory.mkdir(parents=True)
        (directory/'cue-guide-v1.json').write_text('{"schema_version":1,"acknowledged":true}')
        state=directory/'recall-v1.json'
        # Due entries plus future entries expose both queue sections. Start on
        # the grammar card where text leakage into thumbnails would be clearest.
        target=next((c for c in deck if c['cue']=='subject-marker'),deck[0])
        now=int(time.time())
        baseline={ 'stage':1,'due':now+86400,'last_reviewed':now-86400,
                   'reviews':1,'remembered':1,'forgotten':0,'assisted':0 }
        records={d+':'+c['id']:dict(baseline) for c in deck for d in ('recognition','production')}
        records['production:'+target['id']]['due']=now-10
        # Another due card and all future cards remain in scheduler order.
        records['recognition:'+target['id']]['due']=now-5
        state.write_text(json.dumps({'schema_version':1,'cards':records}))
        saved=state.read_bytes()
        capture(args.exe,args.out,'queue-normal-front',env)
        capture(args.exe,args.out,'queue-normal-back',env,[32])
        if state.read_bytes()!=saved: raise RuntimeError('Viewing/revealing rewrote recall state')
        # The entire image-only queue remains identical when the main answer is
        # revealed; no English, kana, audio state or grade UI propagates into it.
        if strip(args.out/'queue-normal-front.bmp',90,235,205,675) != strip(args.out/'queue-normal-back.bmp',90,235,205,675):
            raise RuntimeError('Main reveal changed the cue-only preview column')
        capture(args.exe,args.out,'queue-narrow-front',env,dimensions=(620,720))
        capture(args.exe,args.out,'queue-narrow-back',env,[32],dimensions=(620,720))
        if state.read_bytes()!=saved: raise RuntimeError('Narrow preview changed progress')
        capture(args.exe,args.out,'queue-narrow-scroll',env,
                [("drag",(161,160),(370,485))],dimensions=(620,720))
        if strip(args.out/'queue-narrow-scroll.bmp',50,150,175,485) == strip(args.out/'queue-narrow-front.bmp',50,150,175,485):
            raise RuntimeError('The narrow queue thumb did not drag')
        capture(args.exe,args.out,'queue-narrow-track',env,
                [("click",161,490)],dimensions=(620,720))
        if strip(args.out/'queue-narrow-scroll.bmp',50,150,175,485) != strip(args.out/'queue-narrow-track.bmp',50,150,175,485):
            raise RuntimeError('Narrow dragging did not clamp at the end like a track click')
        capture(args.exe,args.out,'queue-scroll',env,[("drag",(231,197),(500,640))])
        if state.read_bytes()!=saved: raise RuntimeError('Scrolling changed progress')
        if strip(args.out/'queue-scroll.bmp',90,235,205,675) == strip(args.out/'queue-normal-front.bmp',90,235,205,675):
            raise RuntimeError('Dragging the queue thumb did not change its visible previews')
        capture(args.exe,args.out,'queue-scroll-reset',env,
                [("drag",(231,197),(500,640)),("drag",(231,640),(231,180))])
        if strip(args.out/'queue-scroll-reset.bmp',90,222,205,650) != strip(args.out/'queue-normal-front.bmp',90,222,205,650):
            raise RuntimeError('Dragging back to the top did not restore the preview queue')
        capture(args.exe,args.out,'queue-track',env,[("click",231,550)])
        if strip(args.out/'queue-track.bmp',90,222,205,650) == strip(args.out/'queue-normal-front.bmp',90,222,205,650):
            raise RuntimeError('Clicking the scrollbar track did not scroll')
        if state.read_bytes()!=saved: raise RuntimeError('Dragging/track clicks rewrote scores')
        capture(args.exe,args.out,'queue-after-grade',env,[32,ord('2')])
        changed=json.loads(state.read_text())['cards']
        key='production:'+target['id']
        if changed[key]['reviews']!=2 or any(changed[k]!=v for k,v in records.items() if k!=key):
            raise RuntimeError('Grading did not preserve unrelated directions/schedules')
        # The grade cooled both target directions; every other card is future.
        # Clicking Review again must start immediately without rewriting scores.
        history=state.read_bytes()
        capture(args.exe,args.out,'queue-complete-narrow',env,dimensions=(620,720))
        capture(args.exe,args.out,'queue-review-again',env,[("click-once",740,620)])
        if state.read_bytes()!=history: raise RuntimeError('Starting another round changed scores')
        log=(args.out/'queue-review-again.log').read_text(errors='replace')
        if 'Flashcards action review-again' not in log:
            raise RuntimeError('The immediate review button was not activated')
        if 'Flashcards action flip' in log:
            raise RuntimeError('Starting a round revealed an answer without explicit input')
        capture(args.exe,args.out,'queue-repeat-grade',env,[("click-once",740,620),32,ord('2')])
        repeated=json.loads(state.read_text())['cards']
        recognition='recognition:'+target['id']
        if repeated[recognition]['reviews']!=2 or any(repeated[k]!=v for k,v in changed.items() if k!=recognition):
            raise RuntimeError('Immediate round did not save exactly one explicit grade inside cooldown')
        for card in deck:
            if card['image'] not in {'photo-cue.png','list-cue.png','today-cue.png'}:
                continue
            records={d+':'+c['id']:dict(baseline) for c in deck for d in ('recognition','production')}
            records['production:'+card['id']]['due']=now-10
            state.write_text(json.dumps({'schema_version':1,'cards':records}))
            before=state.read_bytes()
            capture(args.exe,args.out,'new-'+card['image'].split('-')[0]+'-back',env,[32])
            if state.read_bytes()!=before: raise RuntimeError('New cue reveal rewrote review history')
        print('Normal/narrow cue-only previews, drag/track scrolling, immediate repeat and exact grade persistence passed.')


if __name__=='__main__': main()
