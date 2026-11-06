---
phase: quick-261106-srk
plan: 01
type: execute
requirements: []
---

# 261106-srk: strict DuckDB-Wasm sqllogic runner (port from anofox-optimize PR #21)

Executed inline. The user asked to port anofox-optimize's strict runner.

1. RED: a probe file that the current runner accepts although every record is wrong
   (`statement error` whose SQL succeeds, 100000 vs 100001, text "001" vs "1").
2. Port `test/wasm/sqllogic.mjs`; add the records this suite uses that optimize's runner
   rejects: `foreach`/`loop` ... `endloop` and `statement maybe`. Internal errors never
   satisfy an expected error (DuckDB's always_fail_error_messages).
3. `test/wasm/run.mjs`: rows read by column index, per-record timeout, unwrapped-query
   count, a missing `--file` fails. Keep CURATED, SKIP_FILES and `--all`.
4. CI: `node test/wasm/sqllogic.mjs --self-test` before the suite in both wasm jobs and in
   the static audits.
5. GREEN: self-test, probe, curated subset, full suite. Triage every failure; change no
   test expectation.
