"""Conversation reader integration using isolated, immutable filesystem events."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('review_results', ROOT / 'tools/review_results.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def uid(index):
    return '12345678-1234-4123-8123-' + format(index, '012x')


class ReviewEvidence(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.now = 1_800_000_000

    def tearDown(self):
        self.temporary.cleanup()

    def event(self, index, outcome='Again', producer=1, sequence=None, direction='recognition', when=None):
        when = self.now - 60 if when is None else when
        result = dict(schema_version=1, kind='draxul.flashcard-review', event_id=uid(index),
                      producer_id=uid(producer), base_checkpoint_id=uid(producer + 10000), producer_sequence=index if sequence is None else sequence,
                      word_id='fixture-word', direction=direction, outcome=outcome,
                      reviewed_at_unix=when, reviewed_at_utc=reader.utc(when),
                      local_review_number=index, explicit=True, source=reader.SOURCE)
        directory = self.root / 'flashcard-reviews' / uid(producer)
        directory.mkdir(parents=True, exist_ok=True)
        checkpoint_path = directory / ('checkpoint-' + result['base_checkpoint_id'] + '.json')
        if not checkpoint_path.exists():
            checkpoint_path.write_text(json.dumps(dict(schema_version=1, kind='draxul.flashcard-checkpoint',
                checkpoint_id=result['base_checkpoint_id'], producer_id=uid(producer),
                created_at_unix=self.now - 10 * 86400, created_at_utc=reader.utc(self.now - 10 * 86400),
                recall={'schema_version': 1, 'cards': {}})))
        (directory / (uid(index) + '.json')).write_text(json.dumps(result))
        return result

    def snapshot(self, events, generated=None, count=None):
        producer = events[0]['producer_id']
        latest = {}
        for event in sorted(events, key=lambda e: e['producer_sequence']):
            latest[event['direction'] + ':' + event['word_id']] = event
        generated = self.now if generated is None else generated
        value = dict(schema_version=1, kind='draxul.flashcard-summary', producer_id=producer,
                     generated_at_unix=generated, generated_at_utc=reader.utc(generated),
                     event_count=len(events) if count is None else count, latest_limit=512, latest=latest,
                     schedule={'schema_version': 1, 'cards': {}},
                     sync=dict(events_seen=len(events), checkpoints_seen=1, unresolved_events=0,
                               read_complete=True, bootstrap_alternatives=False))
        (self.root / 'flashcard-reviews' / producer / 'summary-v1.json').write_text(json.dumps(value))

    def read(self, **kwargs):
        return reader.summarize(self.root, now=self.now, **kwargs)

    def test_later_outcome_clears_only_its_direction_and_reading_preserves_all_bytes(self):
        a = self.event(2)
        b = self.event(3, 'Remembered', when=self.now - 30)
        c = self.event(4, direction='production', when=self.now - 20)
        self.snapshot([a, b, c])
        before = {p: p.read_bytes() for p in self.root.rglob('*.json')}
        result = self.read()
        self.assertEqual(result['event_count'], 3)
        word = result['words']['fixture-word']
        self.assertFalse(word['recognition']['recent_struggle'])
        self.assertTrue(word['production']['recent_struggle'])
        self.assertEqual(word['recognition']['last_observed_outcome'], 'Remembered')
        self.assertEqual(before, {p: p.read_bytes() for p in self.root.rglob('*.json')})

    def test_multi_producer_same_time_conflict_requires_context_then_later_outcome_wins(self):
        self.event(2, producer=1)
        self.event(3, 'Remembered', producer=100)
        word = self.read()['words']['fixture-word']['recognition']
        self.assertTrue(word['ambiguous'])
        self.assertIsNone(word['last_observed_outcome'])
        self.assertFalse(word['recent_struggle'])
        self.event(4, 'Remembered', producer=100, when=self.now - 10)
        self.assertEqual(self.read()['words']['fixture-word']['recognition']['last_observed_outcome'], 'Remembered')

    def test_partial_and_conflicted_copies_are_diagnosed_without_double_counting_or_deletion(self):
        event = self.event(2)
        directory = self.root / 'flashcard-reviews' / uid(1)
        (directory / (uid(2) + ' (conflicted copy).json')).write_text(json.dumps(event))
        (directory / (uid(3) + '.json')).write_text('{"schema_version":')
        (directory / '.unfinished.tmp').write_text('partial')
        result = self.read()
        self.assertEqual(result['event_count'], 1)
        self.assertEqual(result['status'], 'partial')
        self.assertFalse(result['words']['fixture-word']['recognition']['recent_struggle'])
        self.assertEqual(len(list(directory.iterdir())), 5)

    def test_replay_converges_and_bootstrap_preserves_prior_scores_without_fake_events(self):
        self.event(2, producer=1)
        self.event(3, 'Remembered', producer=100)
        result = self.read()
        record = result['schedule']['cards']['recognition:fixture-word']
        self.assertEqual(record['reviews'], 2)
        self.assertEqual(record['stage'], 0)
        self.assertEqual(record['due'], self.now - 60 + 600)
        path = self.root / 'flashcard-reviews' / uid(1) / ('checkpoint-' + uid(10001) + '.json')
        checkpoint = json.loads(path.read_text())
        prior = dict(stage=3, due=self.now + 86400, last_reviewed=self.now - 60,
                     reviews=10, remembered=8, forgotten=1, assisted=1)
        checkpoint['recall']['cards']['recognition:fixture-word'] = prior
        path.write_text(json.dumps(checkpoint))
        result = self.read()
        self.assertEqual(result['schedule']['cards']['recognition:fixture-word'], prior)
        self.assertEqual(result['event_count'], 2)

    def test_orphan_or_partial_history_withholds_schedule_until_bootstrap_arrives(self):
        event = self.event(2)
        path = self.root / 'flashcard-reviews' / uid(1) / ('checkpoint-' + event['base_checkpoint_id'] + '.json')
        original = path.read_text()
        path.write_text('partial')
        result = self.read()
        self.assertEqual(result['status'], 'partial')
        self.assertIsNone(result['schedule'])
        self.assertFalse(result['words']['fixture-word']['recognition']['recent_struggle'])
        path.write_text(original)
        self.assertEqual(self.read()['schedule']['cards']['recognition:fixture-word']['reviews'], 1)

    def test_duplicate_ids_or_producer_sequences_with_different_payloads_are_ignored(self):
        self.event(2)
        self.event(2, 'Remembered', producer=100)
        self.assertEqual(self.read()['event_count'], 0)
        self.event(3, sequence=2)
        self.assertEqual(self.read()['event_count'], 0)

    def test_stale_snapshot_never_overrides_raw_evidence_and_missing_raw_sync_is_labelled(self):
        event = self.event(2, when=self.now - 9 * 86400)
        self.snapshot([event], generated=self.now - 8 * 86400)
        result = self.read()
        self.assertEqual(result['producers'][uid(1)]['summary_status'], 'stale')
        self.assertFalse(result['words']['fixture-word']['recognition']['recent_struggle'])
        recent = self.event(3, when=self.now - 10)
        self.snapshot([event, recent], count=3)
        result = self.read()
        self.assertEqual(result['status'], 'partial')
        self.assertFalse(result['words']['fixture-word']['recognition']['recent_struggle'])

    def test_read_budgets_invalid_events_and_missing_folder_preserve_history(self):
        self.assertEqual(self.read()['status'], 'missing')
        self.event(2)
        self.event(3, 'Remembered')
        self.assertEqual(self.read(max_events=1)['status'], 'partial')
        path = self.root / 'flashcard-reviews' / uid(1) / (uid(3) + '.json')
        event = json.loads(path.read_text()); event['explicit'] = False
        path.write_text(json.dumps(event))
        result = self.read()
        self.assertEqual(result['event_count'], 1)
        self.assertEqual(result['status'], 'partial')
        self.assertTrue(path.exists())


if __name__ == '__main__':
    unittest.main()
