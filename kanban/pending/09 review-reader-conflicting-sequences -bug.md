# Reject all conflicting review identities and producer sequences

Found while validating a local Flashcards rebuild against the Dropbox vocabulary
on 2026-10-07. This is separate from the build-source configuration issue; no
review-reader implementation was changed during that task.

- [ ] Reproduce and correct conflict handling independently of directory iteration order.
- [ ] Cover overlapping event-ID and producer-sequence conflicts in both input orders.
- [ ] Run the Flashcards aggregate and same-cache startup checks.

Evidence: `python3 plugins/flashcards/tests/review_results_tests.py` fails
`test_duplicate_ids_or_producer_sequences_with_different_payloads_are_ignored`
at line 133: event_count is 1, expected 0. Reproduced both in the Flashcards
CTest aggregate and a direct diagnostic rerun on macOS.

`tools/review_results.py` rejects an event-ID conflict before recording/checking
that payload's producer sequence. A later event can therefore escape a sequence
conflict depending on traversal order. Investigate retaining all conflicting
identity/sequence associations before filtering the final event set.

Validation context: local build-source repair embedded all 10 IDs from the shared
vocabulary. The 8-entry Flashcards aggregate took 25.59s; this reader test failed,
while the generator, native review tests and render checks passed. No personal
vocabulary, review contents, binaries or screenshots are included in this card.

Additional validation limitation: `python3 do.py smoke release --skip-build`
and a diagnostic explicit Flashcards `--smoke-test` both returned exit 1 after
Metal initialization, without an initialization error. Their readiness failure
was not diagnosed in the build-source repair. The real packaged Flashcards
screenshot/render runs passed and the captured deck was visually verified.
The startup failure is not established as a consequence of this reader defect.
