# Share flashcard scheduling and explicit results across devices

Requested 2026-10-06 as a separate slice after the published queue/cooldown
checkpoint (product `4daa6c8`, Draxul `764add4b`). The originating request asked
for local implementation. The user's direct "great, commit and push" in this
chat separately authorizes publishing the validated checkpoint.

- [x] Define immutable per-producer grade events, aggregate bootstrap checkpoints and bounded disposable summaries.
- [x] Preserve recall-v1 and add a durable local write-ahead export outbox with safe grade/retry recovery.
- [x] Import shared history at startup and periodically; reconstruct convergent scheduling, eligibility and cooldown with cached offline progress and clear freshness.
- [x] Add a bounded conversation reader: duplicate/conflict/partial-file handling, later directional outcomes, stale-summary diagnostics.
- [x] Test actual persistent grades, interrupted writes, duplicate delivery, offline/reopen retry, two-device convergence, pre-existing scores and stale revealed answers.
- [x] Run the product aggregate, same-cache smoke and Release confirmation; record costs and limitations.
- [x] Configure the user's shared folder, demonstrate isolated fixture results, update the canonical learning README and report initial-export status.

Conversation owns `README.md` and `vocabulary.json` in the personal Learning/Japanese
folder. The app must not modify that vocabulary or its exposure schedule. Only
explicit Again/Remembered produces a result. Events are the evidence; snapshots
are disposable and freshness-labelled. No new vocabulary, automation, reminders,
private repository data or deletion of user history.

Preserve existing recall schema and progress. A local prepared export intent must
be durable before the atomic recall save. On restart, compare its before/after
record with recall to determine whether it committed; never replay a grade merely
because a shared folder is unavailable. Delivery is idempotent by unique event ID.
Each local producer owns its subdirectory/snapshot, avoiding cross-machine
overwrite. Subsequent explicit authorization expands this slice to TWO-WAY
scheduling. Immutable events and pre-sync aggregate checkpoints are authoritative;
local recall-v1 and summaries are derived. No fabricated historical individual
grades. Define baseline alternatives, same-time outcomes, stale grading and
bounded import clearly; preserve all evidence while Dropbox is missing or still
syncing. A revealed answer must stay visible until the user acts.

## Implementation and evidence

Contract and recovery policy: [review-sync.md](../../docs/review-sync.md).
Events reference their producer's immutable aggregate checkpoint. A local
prepared intent precedes recall replacement and resolves against exact before/
after progress. Export acknowledgement failures retry the same UUID. Missing
folders, partial files, orphan events, conflicts and read limits preserve cached
progress. Startup/periodic/pre-grade imports reconstruct the same schedule;
revealed answers stay pinned and full-progress/cooldown guards reject stale grades.
Passive refresh preserves and logs the explanation until explicit retry/refresh.

Multiple pre-sync aggregates select the largest review count per direction,
then time/UUID, retaining alternatives. Covered older events remain archived.
Distinct offline grades both count; same-time Remembered precedes Again. Later
Remembered clears struggling priority. No imported progress emits another grade.

Windows: 634 assertions/28 model cases, eight reader cases and six generator
cases passed in the eight-group product aggregate. Actual native-input fixture
profiles demonstrate offline reopen/delivery, three-device convergence, no extra
events on import/front input, and a revealed stale click being rejected. Final
normal/narrow footer captures retain the explanation without overlap. Full
attempts, failures and costs: [validation.md](../../docs/validation.md).

Actual shared-folder setup used private SDK settings and preserved existing
recall byte-for-byte; it exported one aggregate checkpoint and no grade events.
The canonical personal learning guide now reads verified directional evidence
at conversation start, without conflating scores/exposure/mastery. Personal
paths, journal content and historical scores stay outside Git. Fixture data
stays in ignored build output, separately identified in its report.

macOS execution is transferred to the explicit unchecked shared-scheduling gate
in [card 04](../pending/04%20human-pronunciation-and-speaker-cycling%20-feature.md). POSIX
publication/fsync was inspected; no Mac pass is claimed. The inherited standard
Debug startup timeout is owned by [Draxul card 65](../../../../kanban/pending/65%20windows-validation-timing%20-test.md);
actual-profile Flashcards startup and Release startup passed.

Publication fetch found the independently published Draxul host update
`95493166`; it was fast-forwarded while preserving this slice's changes. Final
post-integration product validation passed 8/8 in 365.51 s, and Release plugin
startup exited 0. Standard Debug smoke's repeated 30 s timeout remains in card
65. Public source is ready for the directly authorized commit/push; actual
learning history and settings remain private.
