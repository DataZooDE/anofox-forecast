---
quick_id: 261106-srk
status: complete
issue: null
commits: [9f9a86f6]
---

# 261106-srk: strict DuckDB-Wasm sqllogic runner

## Cause
The old runner folded a `statement error` record's `----` line and expected message into
the SQL, so the statement failed on a syntax error and passed without running. It compared
any two numeric-looking values within 1e-6 absolute / 1e-4 relative, read BIGINT through a
JS Number, checked no row/column shape, and skipped unknown records (`foreach`,
`statement maybe`) silently.

## Evidence (v1.5.6 wasm_eh artifact CI built from main 2a40cd9a)
- Probe (3 wrong records): old runner 3 passed / 0 failed; strict runner 0 / 3.
- Every one of the suite's 200 `statement error` messages replaced by a string that never
  occurs: old runner 1995 passed / 2 failed (the 2 are `SET threads`); strict runner 220
  message mismatches (200 records, 20 more through `foreach` expansion).
- Self-test: 26 comparator cases, 12 parser checks, 9 statement cases.
- Curated subset: 540 passed, 0 failed. Full suite (`--all`): 4389 passed, 7 failed
  (old runner: 4197 / 8).

## Failures under --all (none in CURATED, none new)
- `SET threads = N`: 6 records in 5 files (union_source.test once per `foreach pio`
  value); the wasm_eh engine is built without threads.
- `extension_comparison.test:52`: expects 159.0, the mean is 159.4; the native runner's 1%
  DOUBLE tolerance hides the wrong expectation.
