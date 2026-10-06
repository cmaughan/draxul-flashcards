# Shared review scheduling

Launch with `--plugin dev.draxul.flashcards --plugin-config
'{"learning_directory":"C:/your/Dropbox/Learning/Japanese"}'`. The absolute
directory is saved in private SDK `learning-settings-v1` storage. Configure the
corresponding local Dropbox path on each device. The directory must already exist
and stay outside the plugin package. An unavailable folder leaves cached
scheduling and pending grades local. The app never writes vocabulary or its
conversation exposure/new-word timers.

## Authoritative history

Each installation retains a UUID producer identity in `review-exports-v1` and
owns `flashcard-reviews/<producer UUID>/` under the learning folder:

- `checkpoint-<UUID>.json`: immutable schema-1 aggregate bootstrap containing
  pre-sync schema-1 recall. Prior counters, stages and due dates survive without
  fabricated historical individual grades.
- `<event UUID>.json`: immutable schema-1 explicit Again/Remembered self-report,
  with producer/sequence, bootstrap reference, word ID, direction, Unix/UTC time
  and local review number. Passive input and imports never produce events.
- `summary-v1.json`: disposable schema-1 snapshot of delivered count, last 512
  directional records, derived schedule and read status. It cannot supply grades
  or scheduling. A positive missing-event count detects incomplete local sync.

Private `review-sync-v1/{checkpoints,events}` mirrors immutable files beside SDK
state. `recall-v1` keeps schema 1 and is reconstructed from baseline plus events.
For a backup, preserve the outbox identity and cache together. Do not copy derived
recall into a new installation as its independent pre-sync history. Missing
identity with an existing cache blocks grading rather than inventing a bootstrap.

## Deterministic replay

For each direction, multiple pre-sync aggregate records select the largest review
count, then latest last-reviewed time, then checkpoint UUID. Unknown overlapping
historical counts are not summed. All alternatives remain archived and cause a
diagnostic. The chosen baseline covers raw events through its last-reviewed time;
those events stay archived but do not count twice. New events replay afterward.

Replay orders events by Unix time, then Remembered before Again at identical
times, then event UUID. Identical IDs count once. Conflicting IDs or producer
sequences block reconstruction and preserve cached scheduling. Distinct offline
reviews both count: disconnected devices cannot know about each other's grades.
A coincident Again/Remembered pair ends in relearning; a later Remembered clears
that directional priority. Events or bootstrap creation over five minutes ahead
are rejected until the local clock catches up; accurate clocks matter.

Both directions use the latest reconstructed grade plus ten minutes as their
shared word cooldown. Only eligible due cards compete for selection; future
reviews are never pulled forward. A revealed answer remains visible as peer
state arrives. Grading rereads full directional progress and shared cooldown
under the process lock. A changed schedule rejects the stale grade and selects
the next eligible card.

## Durability, bounds and freshness

An atomic prepared intent with before/after progress precedes recall replacement.
Restart confirms or aborts it against actual recall; export failure never replays
a grade. Same-directory staged files are flushed and exclusively published;
idempotent retries verify existing immutable contents. Windows uses MoveFileExW
write-through; POSIX uses exclusive hard links and directory fsync. Only summaries
are replaced; raw history is retained.

Imports run on startup, every 30 seconds while visible and before grading;
catch-up polls use one second. A scan admits 64 new files or 50 ms, up to 128
producers and 21,000 entries. Cache limits are 256 checkpoints and 20,000 events,
with five seconds for initial reading. Events are bounded to 8 KiB; checkpoints,
summaries and outbox to 1 MiB. At most 512 pending grades are accepted. Reaching
a limit pauses the affected operation with a diagnostic, preserving evidence.
Partial files, missing bootstraps and detected sync gaps keep prior scheduling
until verification completes. These are local read bounds, not network deadlines.

The status concerns locally visible files. It cannot establish whether Dropbox
is online or has received every peer's latest changes. Stale summaries cannot
override history. Malformed/conflicted files are retained for reconciliation.

## Conversation evidence

Run `py plugins/flashcards/tools/review_results.py --root
'C:/your/Dropbox/Learning/Japanese'` from the Draxul checkout. This bounded,
read-only reader validates events/checkpoints, reconstructs scheduling and reports
explicit outcomes by direction. A recent latest Again can guide a gentle relevant
revisit; a later Remembered clears it. Coincident conflicting reports are labelled
ambiguous as conversation evidence although scheduling follows a deterministic
rule. Partial reads withhold scheduling and recent-struggle inference. Aggregate
counters carry aggregate provenance, not invented historical recall observations.
No result declares mastery or changes the learning journal.
