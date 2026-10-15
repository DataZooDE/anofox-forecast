---
phase: 17-upstream-consumption-diagnostics-parity
fixed_at: 2026-10-15T17:02:28Z
review_path: .planning/phases/17-upstream-consumption-diagnostics-parity/17-REVIEW.md
iteration: 1
findings_in_scope: 1
fixed: 1
skipped: 0
status: all_fixed
---

# Phase 17: Code Review Fix Report

**Fixed at:** 2026-10-15T17:02:28Z
**Source review:** .planning/phases/17-upstream-consumption-diagnostics-parity/17-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 1 (fix_scope = critical_warning; IN-01/IN-02 info findings excluded)
- Fixed: 1
- Skipped: 0

## Fixed Issues

### WR-01: `ts_ljung_box`/`ts_residual_diagnostics` optional args missing `SPECIAL_HANDLING`, unlike their newly-fixed siblings in the same file

**Files modified:** `src/scalar_functions/diagnostics.cpp`, `test/sql/ts_diagnostics_reference.test`
**Commit:** `6ceea19`
**Applied fix:**

Read the current code first (`ts_adf`/`ts_kpss` bodies and `RegisterTsAdfFunction`/
`RegisterTsKpssFunction`/`RegisterTsStationarityFunction` registration) to confirm the established
pattern: the function bodies for `TsLjungBoxFunction` and `TsResidualDiagnosticsFunction` already
read the optional `lags`/`fitted_params`/`alpha` arguments via `ScalarArgReader::IsNull()` and fall
back to the documented default (`lags_val = -1` "auto", `fitted_params_val = 0`, `alpha_val = 0.05`)
— the body-level handling the task asked me to double-check was already correct and required no
change. The bug was purely at registration: the `SPECIAL_HANDLING` flag that lets a NULL argument
reach the function body at all (instead of DuckDB's `DEFAULT_NULL_HANDLING` short-circuiting the
whole row to SQL NULL before the body runs) was never set on these specific overloads, unlike every
sibling overload in the same file.

Set `null_handling = FunctionNullHandling::SPECIAL_HANDLING` on:
- `ts_ljung_box`'s 2-arg (`series, lags`) and 3-arg (`series, lags, fitted_params`) overloads
- `anofox_fcst_ts_ljung_box`'s matching 2-arg/3-arg alias overloads
- `ts_residual_diagnostics`'s 2-arg (`series, alpha`) overload
- `anofox_fcst_ts_residual_diagnostics`'s matching 2-arg alias overload

The 1-arg overloads of both functions were left unchanged (no optional arg, no NULL-means-default
semantics to fix; a NULL series there already falls through `ListDoubleReader::ReadSingleSeries`
under the body's existing NULL handling).

Added sqllogictest coverage to `test/sql/ts_diagnostics_reference.test` (appended after the existing
`arma17`-based Ljung-Box reference block, mirroring the "a NULL regression means 'c'" pattern already
used for `ts_adf`):
- `ts_ljung_box(x, NULL, 2)` matches the pinned default-lags (10) reference values (`df`=8,
  `statistic`≈6.40542007298888, `p_value`≈0.601916478272279) and matches
  `ts_ljung_box(x, 10, 2)` exactly
- `ts_ljung_box(x, 10, NULL)` (NULL `fitted_params`) matches `ts_ljung_box(x, 10)` exactly
- `anofox_fcst_ts_ljung_box(x, NULL, 2)` gets the same fix
- `ts_residual_diagnostics(x, NULL)` matches `ts_residual_diagnostics(x)` (alpha=0.05 default) on
  both `lb_p_value` and `adequate`
- `anofox_fcst_ts_residual_diagnostics(x, NULL)` gets the same fix
- A NULL series still yields SQL NULL on every affected overload
  (`ts_ljung_box(NULL::DOUBLE[], 2)`, `ts_ljung_box(NULL::DOUBLE[], 10, 2)`,
  `ts_residual_diagnostics(NULL::DOUBLE[], 0.05)`)

## Verification

Verification ran in the main checkout (worktree isolation is disabled for this project —
`workflow.use_worktrees: false` in `.planning/config.json` — so no isolated worktree was created;
all edits, the rebuild, and the test runs happened directly on branch `fix/statistical-correctness`
in the working tree at `/home/simonm/projects/duckdb/anofox-forecast`). These numbers are
reproducible by re-running the same commands from that tree/branch.

1. Rebuilt the extension: `GEN=ninja make release` (clean exit, link succeeded:
   `[108/108] repository`; only pre-existing upstream DuckDB header SFINAE warnings, no new
   warnings/errors from the changed file).
2. `build/release/test/unittest "test/sql/ts_diagnostics*"`:
   ```
   Filters: test/sql/ts_diagnostics*
   [0/2] (0%): test/sql/ts_diagnostics.test
   [1/2] (50%): test/sql/ts_diagnostics_reference.test
   [2/2] (100%): test/sql/ts_diagnostics_reference.test
   ===============================================================================
   All tests passed (124 assertions in 2 test cases)
   ```
   (One intermediate test-header typo — a 2-column query tagged `query I` instead of `query II`
   for the "NULL series still yields NULL" case — was caught by this run, fixed, and the test
   re-run clean before proceeding.)
3. `build/release/test/unittest "test/sql/*"`:
   ```
   ==================================================
   All tests passed (50 skipped tests, 2309 assertions in 35 test cases)

   Skipped tests for the following reasons:
   require json: 50
   ```
   (The 50 skipped tests are gated by `require json` and are unrelated to this fix — a pre-existing,
   documented pattern in this codebase, not a regression.)

## Skipped Issues

None — the only in-scope finding (WR-01) was fixed. IN-01 and IN-02 are Info-severity and excluded
by `fix_scope: critical_warning`.

---

_Fixed: 2026-10-15T17:02:28Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
