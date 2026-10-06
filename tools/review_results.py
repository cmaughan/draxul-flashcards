"""Bounded, read-only conversation evidence from immutable explicit review events."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

UUID = re.compile(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')
WORD = re.compile(r'[a-z0-9][a-z0-9._-]{0,95}')
SOURCE = {'product': 'dev.draxul.flashcards', 'version': '0.1.0', 'type': 'self-report'}
EVENT_FIELDS = {'schema_version', 'kind', 'event_id', 'base_checkpoint_id', 'producer_id', 'producer_sequence',
                'word_id', 'direction', 'outcome', 'reviewed_at_unix', 'reviewed_at_utc',
                'local_review_number', 'explicit', 'source'}


def utc(seconds):
    return datetime.fromtimestamp(seconds, timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def unique_fields(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError('duplicate JSON field')
        result[key] = value
    return result


def read_json(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('file exceeds size bound')
    value = json.loads(data, object_pairs_hook=unique_fields)
    pending = [(value, 0)]
    while pending:
        item, depth = pending.pop()
        if depth > 16:
            raise ValueError('JSON exceeds depth bound')
        children = item.values() if isinstance(item, dict) else item if isinstance(item, list) else ()
        pending.extend((child, depth + 1) for child in children)
    return value


def integer(value, maximum):
    return type(value) is int and 0 <= value <= maximum


def validate_event(event, producer, now):
    if (not isinstance(event, dict) or set(event) != EVENT_FIELDS
            or type(event['schema_version']) is not int or event['schema_version'] != 1
            or event['kind'] != 'draxul.flashcard-review' or event['producer_id'] != producer
            or not UUID.fullmatch(str(event['event_id'])) or not UUID.fullmatch(producer)
            or not UUID.fullmatch(str(event['base_checkpoint_id']))
            or not isinstance(event['word_id'], str) or not WORD.fullmatch(event['word_id'])
            or event['direction'] not in ('recognition', 'production')
            or event['outcome'] not in ('Again', 'Remembered') or event['explicit'] is not True
            or event['source'] != SOURCE
            or not integer(event['producer_sequence'], 1_000_000_000) or event['producer_sequence'] == 0
            or not integer(event['local_review_number'], 1_000_000) or event['local_review_number'] == 0
            or not integer(event['reviewed_at_unix'], now + 300)
            or event['reviewed_at_utc'] != utc(event['reviewed_at_unix'])):
        raise ValueError('unsupported or inconsistent explicit event')
    return event


def validate_recall(value):
    if (not isinstance(value, dict) or set(value) != {'schema_version', 'cards'}
            or type(value['schema_version']) is not int or value['schema_version'] != 1
            or not isinstance(value['cards'], dict) or len(value['cards']) > 4000):
        raise ValueError('unsupported recall checkpoint')
    fields = {'stage', 'due', 'last_reviewed', 'reviews', 'remembered', 'forgotten', 'assisted'}
    for key, record in value['cards'].items():
        direction, separator, word = key.partition(':')
        if (not separator or direction not in ('recognition', 'production') or not WORD.fullmatch(word)
                or not isinstance(record, dict) or set(record) != fields
                or any(not integer(record[f], 5 if f == 'stage' else 4_000_000_000_000
                                   if f in ('due', 'last_reviewed') else 1_000_000) for f in fields)
                or record['reviews'] != record['remembered'] + record['forgotten'] + record['assisted']
                or record['due'] < record['last_reviewed']):
            raise ValueError('inconsistent recall checkpoint record')
    return value


def validate_checkpoint(value, producer, now):
    fields = {'schema_version', 'kind', 'checkpoint_id', 'producer_id', 'created_at_unix', 'created_at_utc', 'recall'}
    if (not isinstance(value, dict) or set(value) != fields
            or type(value['schema_version']) is not int or value['schema_version'] != 1
            or value['kind'] != 'draxul.flashcard-checkpoint' or value['producer_id'] != producer
            or not UUID.fullmatch(str(value['checkpoint_id'])) or not UUID.fullmatch(producer)
            or not integer(value['created_at_unix'], now + 300)
            or value['created_at_utc'] != utc(value['created_at_unix'])):
        raise ValueError('unsupported aggregate checkpoint')
    validate_recall(value['recall'])
    return value


def rebuild(checkpoints, events):
    """Mirror the app's deterministic replay; snapshots never supply scheduling."""
    cards, baseline_ids, alternatives = {}, {}, False
    for checkpoint in checkpoints.values():
        cid = checkpoint['checkpoint_id']
        for key, record in checkpoint['recall']['cards'].items():
            previous = cards.get(key)
            alternatives |= previous is not None and previous != record
            if (previous is None or (record['reviews'], record['last_reviewed'], cid)
                    > (previous['reviews'], previous['last_reviewed'], baseline_ids[key])):
                cards[key] = dict(record)
                baseline_ids[key] = cid
    cutoff = {key: record['last_reviewed'] for key, record in cards.items()}
    unresolved = 0
    for event in sorted(events.values(), key=lambda e: (e['reviewed_at_unix'], e['outcome'] == 'Again', e['event_id'])):
        checkpoint = checkpoints.get(event['base_checkpoint_id'])
        if checkpoint is None:
            unresolved += 1
            continue
        if checkpoint['producer_id'] != event['producer_id']:
            raise ValueError('event references another producer checkpoint')
        key = event['direction'] + ':' + event['word_id']
        when = event['reviewed_at_unix']
        if when <= cutoff.get(key, -1):
            continue
        record = cards.setdefault(key, dict(stage=0, due=0, last_reviewed=0, reviews=0, remembered=0, forgotten=0, assisted=0))
        record['reviews'] += 1
        record['last_reviewed'] = when
        if event['outcome'] == 'Again':
            record['forgotten'] += 1
            record['stage'] = 0
            record['due'] = when + 600
        else:
            record['remembered'] += 1
            record['due'] = when + (1, 3, 7, 14, 30, 30)[record['stage']] * 86400
            record['stage'] = min(record['stage'] + 1, 5)
    state = validate_recall({'schema_version': 1, 'cards': cards})
    return state, unresolved, alternatives


def summarize(root, now=None, max_events=20_000, max_producers=128, budget_seconds=5.0):
    now = int(time.time()) if now is None else now
    report = {'schema_version': 1, 'kind': 'draxul.conversation-review-evidence',
              'generated_at_unix': now, 'generated_at_utc': utc(now),
              'status': 'complete', 'event_count': 0, 'checkpoint_count': 0, 'producers': {}, 'words': {},
              'schedule': None, 'bootstrap_alternatives': False,
              'diagnostics': [], 'history_deleted': False}
    deadline = time.monotonic() + budget_seconds
    directory = Path(root) / 'flashcard-reviews'
    if not directory.is_dir():
        report['status'] = 'missing'
        report['diagnostics'].append('Shared review directory unavailable; no outcomes inferred.')
        return report
    events, conflicts, sequences, checkpoints, checkpoint_conflicts = {}, set(), {}, {}, set()
    scanned = 0
    inspected_producers = 0

    def diagnose(message):
        report['status'] = 'partial'
        if len(report['diagnostics']) < 100:
            report['diagnostics'].append(message)

    try:
        for producer_dir in directory.iterdir():
            if inspected_producers >= max_producers * 2 or time.monotonic() > deadline:
                diagnose('Directory/time read budget reached; history preserved.')
                break
            inspected_producers += 1
            producer = producer_dir.name
            if producer_dir.is_symlink() or not producer_dir.is_dir() or not UUID.fullmatch(producer):
                diagnose('Non-producer/conflicted entry ignored: ' + producer)
                continue
            if len(report['producers']) >= max_producers or time.monotonic() > deadline:
                diagnose('Producer/time read budget reached; history preserved.')
                break
            info = {'observed_events': 0, 'summary_status': 'missing'}
            report['producers'][producer] = info
            for path in producer_dir.iterdir():
                if path.name == 'summary-v1.json':
                    continue
                if scanned >= max_events + 256 or len(events) >= max_events or time.monotonic() > deadline:
                    diagnose('Event/time read budget reached; history preserved.')
                    break
                scanned += 1
                is_checkpoint = path.name.startswith('checkpoint-')
                identity = path.stem[11:] if is_checkpoint else path.stem
                if path.is_symlink() or not path.is_file() or path.suffix != '.json' or not UUID.fullmatch(identity):
                    diagnose('Partial/conflicted entry ignored: ' + path.name)
                    continue
                try:
                    if is_checkpoint:
                        if len(checkpoints) >= 256:
                            raise ValueError('checkpoint read limit reached')
                        checkpoint = validate_checkpoint(read_json(path, 1024 * 1024), producer, now)
                        cid = checkpoint['checkpoint_id']
                        if identity != cid:
                            raise ValueError('filename/checkpoint ID mismatch')
                        if cid in checkpoints and checkpoints[cid] != checkpoint:
                            checkpoint_conflicts.add(cid)
                            raise ValueError('checkpoint ID conflict')
                        checkpoints[cid] = checkpoint
                        continue
                    event = validate_event(read_json(path, 8192), producer, now)
                    eid = event['event_id']
                    if path.stem != eid:
                        raise ValueError('filename/event ID mismatch')
                    if eid in events and events[eid] != event:
                        conflicts.add(eid)
                        raise ValueError('event ID conflict')
                    sequence = (producer, event['producer_sequence'])
                    if sequence in sequences and sequences[sequence] != eid:
                        conflicts.update((eid, sequences[sequence]))
                        raise ValueError('producer sequence conflict')
                    events[eid] = event  # Identical event IDs are counted once.
                    sequences[sequence] = eid
                except (OSError, ValueError, TypeError, KeyError, OverflowError, RecursionError) as error:
                    diagnose('Invalid event ' + path.name + ': ' + str(error))
    except OSError as error:
        diagnose('Shared review scan unavailable: ' + str(error))
    for eid in conflicts:
        events.pop(eid, None)
    for cid in checkpoint_conflicts:
        checkpoints.pop(cid, None)
    report['event_count'] = len(events)
    report['checkpoint_count'] = len(checkpoints)
    try:
        schedule, unresolved, alternatives = rebuild(checkpoints, events)
        report['bootstrap_alternatives'] = alternatives
        if unresolved:
            diagnose('Raw events are awaiting their aggregate bootstrap checkpoint; schedule withheld.')
    except (ValueError, KeyError, TypeError) as error:
        schedule = None
        diagnose('Schedule reconstruction unavailable: ' + str(error))
    by_producer, by_direction, counts = {}, {}, {}
    for event in events.values():
        producer = event['producer_id']
        by_producer.setdefault(producer, []).append(event)
        pair = (event['word_id'], event['direction'])
        counts[pair] = counts.get(pair, 0) + 1
        key = (event['word_id'], event['direction'], producer)
        if key not in by_direction or by_direction[key]['producer_sequence'] < event['producer_sequence']:
            by_direction[key] = event
    for producer, info in report['producers'].items():
        observed = by_producer.get(producer, [])
        info['observed_events'] = len(observed)
        snapshot = directory / producer / 'summary-v1.json'
        if time.monotonic() > deadline:
            info['summary_status'] = 'not_read_budget'
            diagnose('Summary/time read budget reached; history preserved.')
            continue
        if not snapshot.is_file():
            continue  # Raw events remain valid without a disposable snapshot.
        try:
            summary = read_json(snapshot, 1024 * 1024)
            fields = {'schema_version', 'kind', 'producer_id', 'generated_at_unix', 'generated_at_utc', 'event_count', 'latest_limit', 'latest', 'schedule', 'sync'}
            if (not isinstance(summary, dict) or set(summary) != fields
                    or type(summary['schema_version']) is not int or summary['schema_version'] != 1
                    or summary['kind'] != 'draxul.flashcard-summary' or summary['producer_id'] != producer
                    or not integer(summary['generated_at_unix'], now + 300)
                    or summary['generated_at_utc'] != utc(summary['generated_at_unix'])
                    or not integer(summary['event_count'], 1_000_000_000) or summary['latest_limit'] != 512
                    or not isinstance(summary['latest'], dict) or len(summary['latest']) > 512 or snapshot.is_symlink()):
                raise ValueError('invalid disposable summary')
            validate_recall(summary['schedule'])
            sync = summary['sync']
            if (not isinstance(sync, dict) or set(sync) != {'events_seen', 'checkpoints_seen', 'unresolved_events', 'read_complete', 'bootstrap_alternatives'}
                    or not integer(sync['events_seen'], 20_000) or not integer(sync['checkpoints_seen'], 256)
                    or not integer(sync['unresolved_events'], 20_000)
                    or type(sync['read_complete']) is not bool or type(sync['bootstrap_alternatives']) is not bool):
                raise ValueError('invalid disposable sync status')
            expected = {}
            for event in observed:
                key = event['direction'] + ':' + event['word_id']
                if key not in expected or expected[key]['producer_sequence'] < event['producer_sequence']:
                    expected[key] = event
            expected = dict(sorted(expected.items(), key=lambda item: item[1]['producer_sequence'])[-512:])
            if (summary['event_count'] != len(observed) or summary['latest'] != expected
                    or summary['generated_at_unix'] < max((e['reviewed_at_unix'] for e in observed), default=0)):
                info['summary_status'] = 'out_of_date_or_incomplete_sync'
                report['diagnostics'].append('Producer snapshot does not match observed raw events: ' + producer)
                if summary['event_count'] > len(observed):
                    diagnose('Snapshot reports missing raw events; incomplete sync suspected: ' + producer)
            elif now - summary['generated_at_unix'] > 7 * 86400:
                info['summary_status'] = 'stale'
                report['diagnostics'].append('Producer snapshot is over seven days old: ' + producer)
            else:
                info['summary_status'] = 'verified_against_observed_events'
        except (OSError, ValueError, KeyError, TypeError, OverflowError, RecursionError) as error:
            info['summary_status'] = 'invalid'
            diagnose('Ignored disposable snapshot ' + producer + ': ' + str(error))
    if report['status'] == 'complete':
        report['schedule'] = schedule
    grouped = {}
    for (word, direction, _), event in by_direction.items():
        grouped.setdefault((word, direction), []).append(event)
    for (word, direction), latest_by_producer in grouped.items():
        latest_time = max(e['reviewed_at_unix'] for e in latest_by_producer)
        candidates = [e for e in latest_by_producer if e['reviewed_at_unix'] == latest_time]
        outcomes = sorted({e['outcome'] for e in candidates})
        ambiguous = len(outcomes) != 1
        outcome = None if ambiguous else outcomes[0]
        report['words'].setdefault(word, {})[direction] = {
            'last_observed_outcome': outcome, 'last_observed_at_unix': latest_time,
            'last_observed_at_utc': utc(latest_time), 'ambiguous': ambiguous,
            'coincident_outcomes': outcomes, 'observed_event_count': counts[(word, direction)],
            'recent_struggle': report['status'] == 'complete' and outcome == 'Again' and now - latest_time <= 7 * 86400}
        if ambiguous:
            report['diagnostics'].append('Coincident cross-producer outcomes need context: ' + word + '/' + direction)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path, help='Configured shared learning folder')
    args = parser.parse_args()
    print(json.dumps(summarize(args.root), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
