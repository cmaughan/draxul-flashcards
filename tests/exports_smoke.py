"""Actual package: isolated fixture grades, offline outbox, reopening and export."""
import argparse
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import uuid
from render_smoke import capture

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if sys.platform != 'win32':
        print('Packaged native input demonstration is Windows-only; storage/reader integration is portable.')
        return
    embed = module('embed_deck', ROOT / 'tools/embed_deck.py')
    reader = module('review_results', ROOT / 'tools/review_results.py')
    deck = embed.build_deck(args.source, ROOT / 'assets/artwork.json')['cards']
    shared = args.out / ('fixture-shared-日本語-' + uuid.uuid4().hex)
    with ExitStack() as stack:
        def device():
            temporary = stack.enter_context(tempfile.TemporaryDirectory(prefix='draxul-export-fixture-'))
            env = dict(os.environ, APPDATA=temporary, LOCALAPPDATA=temporary, SDL_AUDIODRIVER='dummy')
            directory = Path(temporary) / 'draxul/plugin-config/dev.draxul.flashcards/state'
            directory.mkdir(parents=True)
            (directory / 'cue-guide-v1.json').write_text('{"schema_version":1,"acknowledged":true}')
            return env, directory / 'recall-v1.json', directory / 'review-exports-v1.json'
        env, recall, outbox = device()
        peer_env, peer_recall, peer_outbox = device()
        capture(args.exe, args.out, 'export-offline-grade', env, [32, ord('1')],
                config={'learning_directory': str(shared)})
        state = json.loads(recall.read_text())['cards']
        key = 'recognition:' + deck[0]['id']
        if len(state) != 1 or state[key]['forgotten'] != 1:
            raise RuntimeError('Isolated actual grade did not persist exactly once')
        history = recall.read_bytes()
        pending = json.loads(outbox.read_text())['pending']
        if len(pending) != 1 or shared.exists():
            raise RuntimeError('Offline grade lost its intent or created a fake shared root')
        event = pending[0]
        capture(args.exe, args.out, 'export-offline-reopened', env, dimensions=(620, 720))
        if recall.read_bytes() != history or json.loads(outbox.read_text())['pending'] != pending:
            raise RuntimeError('Offline reopen duplicated a grade or changed its event identity')
        # Independently graded on a second offline machine before either can sync.
        capture(args.exe, args.out, 'export-peer-offline', peer_env, [32, ord('2')],
                config={'learning_directory': str(shared)})
        peer_pending = json.loads(peer_outbox.read_text())['pending']
        if len(peer_pending) != 1 or peer_pending[0]['outcome'] != 'Remembered' or shared.exists():
            raise RuntimeError('Second offline device did not retain its explicit grade')
        shared.mkdir()
        capture(args.exe, args.out, 'export-delivered', env)
        if recall.read_bytes() != history or json.loads(outbox.read_text())['pending']:
            raise RuntimeError('Delivery changed recall or did not acknowledge its outbox')
        def event_files():
            return [p for p in shared.rglob('*.json') if p.name != 'summary-v1.json' and not p.name.startswith('checkpoint-')]
        events = event_files()
        if len(events) != 1 or json.loads(events[0].read_text()) != event:
            raise RuntimeError('Shared delivery lost or duplicated its immutable event')
        capture(args.exe, args.out, 'export-peer-delivered', peer_env)
        capture(args.exe, args.out, 'export-converged', env)
        converged = json.loads(recall.read_text())
        if (converged != json.loads(peer_recall.read_text()) or converged['cards'][key]['reviews'] != 2
                or converged['cards'][key]['remembered'] != 1 or converged['cards'][key]['forgotten'] != 1
                or len(event_files()) != 2):
            raise RuntimeError('Two independently offline grades did not converge exactly once')
        history = recall.read_bytes()
        capture(args.exe, args.out, 'export-repeat', env, [ord('R'), ord('2')])
        if recall.read_bytes() != history or len(event_files()) != 2:
            raise RuntimeError('Front input/retry changed review progress or duplicated delivery')
        fresh_env, fresh_recall, fresh_outbox = device()
        capture(args.exe, args.out, 'export-fresh-device', fresh_env,
                config={'learning_directory': str(shared)})
        if json.loads(fresh_recall.read_text()) != converged or json.loads(fresh_outbox.read_text())['delivered_count'] != 0:
            raise RuntimeError('Fresh device lost shared scores or fabricated an imported grade event')
        result = reader.summarize(shared)
        if result['event_count'] != 2 or result['status'] != 'complete' or result['schedule'] != converged:
            raise RuntimeError('Conversation reader did not reconstruct the actual converged schedule')
        stale_shared = args.out / ('fixture-stale-' + uuid.uuid4().hex)
        stale_shared.mkdir()
        stale_env, stale_recall, stale_outbox = device()
        def peer_arrives():
            # Copy an actual isolated explicit result, retaining its original ID
            # and empty bootstrap. This never touches personal learning history.
            original_box = json.loads(outbox.read_text())
            producer = stale_shared / 'flashcard-reviews' / event['producer_id']
            producer.mkdir(parents=True)
            checkpoint = original_box['checkpoint']
            (producer / ('checkpoint-' + checkpoint['checkpoint_id'] + '.json')).write_text(json.dumps(checkpoint))
            (producer / (event['event_id'] + '.json')).write_text(json.dumps(event))
        capture(args.exe, args.out, 'export-stale-rejected', stale_env, [32, peer_arrives, ord('2')],
                config={'learning_directory': str(stale_shared)})
        if (json.loads(stale_recall.read_text())['cards'][key]['reviews'] != 1
                or json.loads(stale_outbox.read_text())['delivered_count'] != 0):
            raise RuntimeError('Revealed stale answer was graded twice')
        if 'stale grade was not applied' not in (args.out / 'export-stale-rejected.log').read_text(errors='replace'):
            raise RuntimeError('Stale rejection explanation was cleared by passive refresh')
        report = {'fixture_demonstration': True, 'user_grades_created': False,
                  'shared_directory': str(shared), 'event_files': [str(p) for p in event_files()],
                  'devices_converged': 3, 'stale_grade_rejected': True,
                  'local_record': converged['cards'][key], 'conversation_evidence': result}
        (args.out / 'fixture-export-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
        print('Actual offline grades, durable reopen, Unicode shared path, retry, three-device convergence and reader replay passed.')


if __name__ == '__main__':
    main()
