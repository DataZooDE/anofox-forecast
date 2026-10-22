## Deferred Items

### Plan 19-01

- `_ts_fill_forward_native` has pre-existing, WASM-only bugs on calendar frequencies and on TIMESTAMP + hourly frequency, independent of the #289 nested-query/identifier/frequency-ignored bugs fixed in this plan.
  status: open
  **What:** On the identical v1.5.6 `wasm_eh` artifact (native vs. the Node DuckDB-Wasm harness):
  - `'1mo'` (monthly): WASM returns 2 rows, native returns 4.
  - `'1q'` (quarterly): WASM returns 216 rows, native returns 4.
  - `'1y'` (yearly): WASM returns 1 row, native returns 4.
  - TIMESTAMP + `'1h'` (hourly): WASM raises `Maximum call stack size exceeded` and silently corrupts subsequent query results in the same connection (a fresh quarterly query run right after the crash returned the same wrong 216-row result as when run in isolation, so the corruption is not a pure cascade -- there is a second, independent bug on the quarterly path too).
  - `'1d'` (daily), `'2d'`, and raw-integer-offset (`'1'` on a BIGINT date column) are correct in WASM, matching native exactly.
  **Root cause:** Unknown -- not root-caused in Phase 19 (out of CONTEXT.md's locked scope, which covers only the nested-query hang/trap, unquoted identifiers, and ignored frequency). Likely candidate per 19-RESEARCH.md: calendar-interval arithmetic behaving differently under wasm32-unknown-emscripten vs. native x86_64 (possibly floating-point, `usize` width, or a date-library difference) -- a hypothesis, not verified.
  **Evidence:** `.planning/phases/19-wasm-traps-distribution-pipeline/19-RESEARCH.md`, "Common Pitfalls" Pitfall 1.
  **Mitigation applied here:** Every WASM-gated assertion in `test/sql/ts_fill_forward_operator.test` uses only `'1d'` or `'2d'`. The pre-existing hourly `ts_fill_forward_by` pin was moved to `test/sql/ts_fill_forward_native.test` (already WASM skip-listed for an unrelated `___trap` Emscripten issue), where it still runs natively.
  **Candidate for:** a separate GitHub issue (not filed in this plan) tracking the calendar/hourly `_ts_fill_forward_native` WASM-only bug.
