---
phase: 15-detection-decomposition-on-fdars-0-47
fixed_at: 2026-10-14T09:13:32Z
review_path: .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
iteration: 2
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 15: Code Review Fix Report (iteration 2 — re-review of the --auto fix loop)

**Fixed at:** 2026-10-14T09:13:32Z
**Source review:** .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
**Iteration:** 2

**Summary:**
- Findings in scope (critical + warning): 4 (CR-04, CR-05, WR-03, plus the FFI-side
  completion of CR-05 discovered during implementation — see below)
- Fixed: 4
- Skipped: 0

Verification ran in the main working tree on branch `fix/statistical-correctness`
(`workflow.use_worktrees` is `false` for this project per `.planning/config.json`
and project memory — no isolated worktree was created; all edits, builds, and
tests below are directly reproducible from the current checkout).

## Fixed Issues

### CR-04: `ts_mstl_decomposition_by` silently drops groups with insufficient data

**Files modified:** `src/table_functions/ts_mstl_decomposition_native.cpp`, `test/sql/ts_detection_reference.test`
**Commit:** `f00bba4` — fix(15): CR-04 stop ts_mstl_decomposition_by from silently dropping groups (code); test assertions in `282839f`
**Applied fix:** The native streaming finalize loop's `else { continue; }` branch
(reached for the documented default `insufficient_data='fail'`, and also for
`'none'` before the fix) silently dropped the group's row with no error and no
diagnostic. Replaced with:
- `'fail'` (mode 0): on `!success`, raise `InvalidInputException` naming the
  group (`row.group_value.ToString()`) and the underlying Rust error message
  (e.g. `"ts_mstl_decomposition_by: group '1': Insufficient data: need at
  least 2001 observations, got 20"`), instead of `continue`-ing past it.
- `'trend'` (mode 1): unaffected — the FFI call already returns a successful
  trend-only (`supsmu`) decomposition for this mode, which the pre-existing
  `success && mstl_result.decomposition_applied` branch already handles
  correctly.
- `'none'` (mode 2): emits a row with empty `trend`/`seasonal`/`remainder`/
  `periods`, matching the scalar `_ts_mstl_decomposition`'s CR-01 shape for
  the identical condition, so the table and scalar surfaces now agree.

A second, independent bug was found and fixed in the same function:
`ParseInsufficientDataMode` recognized the strings `"fail"`/`"skip"`/`"pad"`,
but the `ts_mstl_decomposition_by` macro (`src/macros/ts_macros.cpp`) and its
documentation (`docs/api/05a-decomposition.md`) only ever pass
`"fail"`/`"trend"`/`"none"`. Since `"trend"` and `"none"` never matched any of
the recognized strings, both silently fell through to the default (`0` =
`fail`) regardless of what the caller requested — meaning `insufficient_data
= 'trend'` and `'none'` were **completely non-functional** on the table
macro before this fix, independent of the row-dropping bug itself. Fixed to
match the scalar `_ts_mstl_decomposition`'s `ParseInsufficientDataMode`
(`src/table_functions/ts_decomposition.cpp`) and
`InsufficientDataMode::from_int` (`crates/anofox-fcst-core/src/decomposition.rs`)
exactly: `"trend"` → 1, `"none"` → 2, default → 0.

**Git history check (per task guidance):** confirmed via `git log --oneline --all
-- src/table_functions/ts_mstl_decomposition_native.cpp` that the row-dropping
`continue` branch predates Phase 15 — present since `5918b37` / `f1758f7`
(both before plan 15-04's `09e93ff`). Not a regression introduced by this
phase's iteration-1 fix commits.

**Test note:** the regression assertions exercise the fix via the internal
`_ts_mstl_decomposition_native` table function directly, not the public
`ts_mstl_decomposition_by` macro. The macro's expansion unconditionally calls
`to_json(params)` — even for an empty `MAP{}` — which requires the `json`
extension. That extension is unavailable in this native, non-json-gated
build (confirmed: `make test_release` reports exactly `require json: 50`
files skipped for the whole suite, and `ts_mstl_decomposition_by`'s only
other test caller, `test/sql/ts_varchar_edge_cases.test`, carries its own
`require json` directive). This is a pre-existing, independent gap — the
macro has apparently never been exercised by a non-json-gated test — noted
here, not fixed, since it is out of CR-04's scope and `_ts_mstl_decomposition_native`
reaches the exact same finalize-loop code the macro expands into. Manually
confirmed end-to-end against the macro itself (with `INSTALL json; LOAD
json;`) using the review's own live repro: `ts_mstl_decomposition_by('sales',
product_id, date, qty, MAP{'seasonal_periods': '[1000]'})` on the review's
exact 2-group table now raises `Invalid Input Error: ts_mstl_decomposition_by:
group '1': Insufficient data: need at least 2001 observations, got 20`
instead of silently returning only `product_id=2`.

### CR-05: 3 of 9 period-validating SQL functions discarded the CR-03 error and returned NULL

**Files modified:** `src/table_functions/ts_detrend.cpp`, `src/table_functions/ts_peaks.cpp`, `crates/anofox-fcst-ffi/src/lib.rs`, `test/sql/ts_detection_reference.test`
**Commits:** `0562bcf` (C++ wrappers), `86802cc` (FFI completion, shared with WR-03), `282839f` (tests)
**Applied fix:** Added the missing `if (error.code == INVALID_INPUT) { throw
InvalidInputException(...); }` branch to `ts_detect_seasonality_changes`,
`ts_detect_amplitude_modulation` (both in `src/table_functions/ts_detrend.cpp`
— the review's cited file, `ts_seasonality.cpp`, was stale; these functions
now live in `ts_detrend.cpp`), and `ts_analyze_peak_timing`
(`src/table_functions/ts_peaks.cpp`), mirroring their 6 working siblings.

**Root-cause finding beyond the review's stated fix:** adding the C++ check
alone did not fix the bug — live testing showed all three functions still
returned `NULL` for `'nan'::DOUBLE`, `0.0`, and `-5.0` after the C++-only
change. Investigation found that these same three functions' FFI entry
points (`anofox_ts_detect_seasonality_changes`,
`anofox_ts_detect_amplitude_modulation`, `anofox_ts_analyze_peak_timing` in
`crates/anofox-fcst-ffi/src/lib.rs`) **hardcoded `ErrorCode::ComputationError`**
in their `Ok(Err(e))` branch regardless of the underlying Rust error type,
instead of using the `ffi_error_code(&e)` helper every other
period-validating function in the file already uses to map
`InvalidParameter`/`InvalidInput` → `ErrorCode::InvalidInput`. This meant
`error.code` could never actually equal `INVALID_INPUT` at the C++ layer for
these three functions, so the newly-added throw branch was unreachable dead
code and every failure — including the CR-03 `InvalidParameter` for a
NaN/non-finite/non-positive period — still silently became a SQL `NULL`.
Fixed by switching all three to `ffi_error_code(&e)`, matching
`ts_detrend`/`ts_decompose`/`ts_seasonal_strength`/`ts_seasonal_strength_windowed`'s
existing pattern. (`ts_classify_seasonality`'s FFI entry point had the
identical hardcoding bug; fixed incidentally in the same commit even though
its C++ caller already works correctly today via its own independent
`std::isfinite` pre-check added in iteration 1 — this closes the same latent
gap before a future refactor of that pre-check could expose it.)

**Verification:** live-tested against the built extension —
`ts_detect_seasonality_changes(LIST(value), 'nan'::DOUBLE)` now raises
`Invalid Input Error: ts_detect_seasonality_changes: Invalid parameter
'period' = 'NaN': Period must be a positive finite number` instead of
returning `NULL`; confirmed for all three functions.

### WR-03: stale duplicate FFI `period <= 0.0` pre-checks produced an inconsistent message for `-Infinity`

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`, `test/sql/ts_detection_reference.test`
**Commit:** `86802cc` (shared with CR-05's FFI completion, since both findings land in the same 7 functions in the same file — see that commit's message for why they weren't split by hunk)
**Applied fix:** Deleted all 7 stale `if period <= 0.0 { ...; return false; }`
pre-checks at the FFI boundary (`anofox_ts_analyze_peak_timing`,
`anofox_ts_decompose`, `anofox_ts_seasonal_strength`,
`anofox_ts_seasonal_strength_windowed`, `anofox_ts_classify_seasonality`,
`anofox_ts_detect_seasonality_changes`, `anofox_ts_detect_amplitude_modulation`),
per the review's first suggested option ("delete these 7 FFI-level
pre-checks"). Every one of these 7 calls falls through to a core-level
`!period.is_finite() || period <= 0.0` check (fixed by CR-03 in iteration 1)
that is strictly a superset of the deleted check, so there is now exactly
one source of truth for "period must be a positive finite number."

**Found and fixed a knock-on stale assertion:** removing the stale
pre-check changed `ts_decompose_seasonal([...], 0.0)`'s error message from
the old `"Period must be positive"` to the unified `"Period must be a
positive finite number"` — a pre-existing SQL regression test in
`test/sql/ts_detection_reference.test` asserted the old wording and started
failing `make test_release` immediately after this fix. Updated that
assertion to the new, correct, unified message (documented as a WR-03
consequence in the test file).

**Verification:** `ts_seasonal_strength(values, '-inf'::DOUBLE)` now raises
`Invalid Input Error: ts_seasonal_strength: Invalid parameter 'period' =
'-inf': Period must be a positive finite number` — the same message every
other non-finite/non-positive period path produces, resolving the
inconsistency the review identified (previously `"Period must be
positive"`, the old wording).

### IN-02: no code change required

Not in scope (`fix_scope: critical_warning`). CR-04 was fixed as described
above (raises an error for `'fail'`, emits a row for `'trend'`/`'none'`), so
per IN-02's own fix guidance ("None required if CR-04 is fixed as
described") no documentation update is needed — the macro's existing
docstring ("`insufficient_data` is 'fail' (default), 'trend', or 'none'")
already matches the corrected behavior and was never claiming row-dropping
in the first place.

## Skipped Issues

None — all 4 in-scope findings (2 critical, 1 warning, plus the CR-05 FFI
root-cause completion folded into the same fix) were fixed. IN-02 required
no action per its own guidance (see above) and was out of scope regardless.

## Verification

- `cargo fmt --all --check`: clean.
- `RUSTUP_TOOLCHAIN=1.89.0 cargo test --workspace`: all passed (431 unit tests
  in `anofox-fcst-core`, same as the iteration-1 baseline — no regressions
  — plus all integration/doc tests across the workspace, 0 failures).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: succeeded (only
  pre-existing, unrelated SFINAE compiler warnings from DuckDB headers, same
  as iteration 1's baseline).
- `make test_release`: **all tests passed — 2070 assertions in 32 test
  cases** (50 json-gated tests skipped, as expected), up from the iteration-1
  baseline of 2042 assertions. `test/sql/ts_detection_reference.test`
  (non-json-gated) carries all new iteration-2 regression assertions: the
  CR-04 2-group `_ts_mstl_decomposition_native` block (fail/trend/none x
  row-count + shape checks), the CR-05/WR-03 `foreach period nan inf -inf
  -5.0 0.0` loop across 3 functions, and the corrected `ts_decompose_seasonal`
  stale-message assertion.
- `src/include/anofox_fcst_ffi.h` was **not** regenerated/committed: no FFI
  function signature changed in any of this iteration's fixes (only
  internal error-code mapping and removal of dead pre-check code; no
  parameter, return type, or function name changed).
- Manually re-ran every literal repro query from REVIEW.md against the
  rebuilt extension (`build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`,
  confirmed rebuilt at the HEAD commit of this fix session) and confirmed
  each now produces the documented/expected behavior instead of the
  reported defect — see the "Verification" notes under CR-04 and CR-05
  above for the exact commands and output.

---

_Fixed: 2026-10-14T09:13:32Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
