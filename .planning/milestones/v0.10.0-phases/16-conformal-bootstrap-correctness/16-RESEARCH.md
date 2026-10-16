# Phase 16: Conformal & Bootstrap Correctness - Research

**Researched:** 2026-10-12
**Domain:** Distribution-free prediction intervals (split/asymmetric/adaptive conformal, bootstrap) in a Rust-core/C++-marshal/SQL-macro DuckDB extension
**Confidence:** HIGH (all code-path claims are `[VERIFIED]` by reading the exact file/line this session; statistical-method claims about what the *correct* formula is are `[CITED]`/`[ASSUMED]` per standard references)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### SQL contract (RUST-04)
- **D-01:** `ts_conformal_by`, `ts_conformal_apply_by` and `ts_interval_width_by` get a required date/step column parameter. The macros order and pair by it, and lower/upper stay paired per row: no independent sorting (D4, D5). This is a breaking signature change, documented in the CHANGELOG.
- **D-02:** `coverage` becomes the EMPIRICAL coverage on the calibration data, computed in Rust. A new `nominal_level` column carries the requested level. All residuals are computed in Rust, not SQL (D6).
- **D-03:** `ts_conformal_calibrate` calibrates per group by default, so groups with different residual scales get different quantiles. Pooled calibration happens only when explicitly requested via `params := {'pooled': true}`.
- **D-04:** Parallelism stays GROUP BY + scalar/aggregate, with no table-in/out functions (project rule).

#### Split, asymmetric and adaptive semantics (CONF-02, CONF-03, CONF-04)
- **D-05:** When ⌈(n+1)(1−α)⌉ > n: NULL bounds and width (the "unbounded" interval in SQL) plus a `calibration_insufficient` indicator (or n reported). Never clamp to the max residual (G12).
- **D-06:** α ∉ (0,1) raises an error naming the value. NaN/NULL residuals are filtered before quantiles are taken, never a panic (G16), and the number of residuals used is reported as `n_calibration`.
- **D-07:** Asymmetric conformal takes its bounds from the α/2 and 1−α/2 quantiles of SIGNED residuals, with the same finite-sample correction. A seeded Monte Carlo test in Rust requires coverage within ±2pp of nominal on symmetric and skewed residuals (G13; it is about 40% too wide today).
- **D-08:** Adaptive conformal takes the difficulty d as an explicit per-row input (column/list) at both calibration and prediction. Scores are |r|/d and intervals q·d. There is no batch-mean normalisation, so a row's interval is independent of batch composition (G14).

#### Jackknife+, bootstrap, testing and plan split (CONF-05, CONF-01)
- **D-09:** `'JackknifePlus'` is removed. All of its aliases (`jackknife+`, `jackknifeplus`, `jackknife_plus`, `jk+`) raise an error explaining it was split conformal and pointing to `'split'` / `'crossval'` (G15). A true jackknife+ needs leave-one-out refits that do not fit the GROUP BY architecture, so it is deferred.
- **D-10:** The bootstrap residual sign is fixed (G2). A seeded right-skewed (log-normal) residual fixture must put the long tail on the upper side, checked against a numpy simulation of the same construction.
- **D-11:** References: independent numpy implementations in `validation/reference/python` (split/asymmetric/adaptive quantiles, bootstrap paths) and Rust `crates/anofox-fcst-core/tests/conformal_reference.rs` with the Monte Carlo coverage tests. A new NON-json-gated `test/sql/ts_conformal_reference.test` holds the SQL assertions; existing ts_conformal.test / ts_conformal_coverage.test are json-gated, and un-gating them is Phase 18. Breaking changes get CHANGELOG before/after rows; docs and the backtest skill are updated; no compatibility flag.
- **D-12:** 3 plans: (1) SQL contract tracer [RUST-04]; (2) split/asymmetric/adaptive core [CONF-02, CONF-03, CONF-04]; (3) bootstrap sign + jackknife+ removal + docs/skill/CHANGELOG [CONF-01, CONF-05].

### Claude's Discretion
- Exact parameter names for the step column and difficulty input (consistent with existing `_by` macro naming), Monte Carlo sample sizes and seeds, and the internal structure of conformal.rs.

### Deferred Ideas (OUT OF SCOPE)
- A true jackknife+ (leave-one-out refits), if a refit-capable architecture is ever added.
- Removing the json gate from ts_conformal.test / ts_conformal_coverage.test (Phase 18 / TEST-03).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RUST-04 | `ts_conformal_by`, `ts_conformal_apply_by`, `ts_interval_width_by`, `ts_conformal_calibrate` preserve horizon order (date/step column) and pairing; residuals and empirical coverage computed in Rust; per-group calibration supported [D4-D6] | §SQL Macro Audit, §Architecture Patterns Pattern 1-3, §Code Examples |
| CONF-01 | Bootstrap paths add residuals with the correct sign; skewed-residual fixture gives correct-side intervals [G2] | §Common Pitfalls Pitfall 1, §Code Examples (bootstrap sign fix) |
| CONF-02 | Split conformal returns infinite/NULL width (or errors) when ⌈(n+1)(1−α)⌉ > n; α ∉ (0,1) rejected; NaN residuals filtered [G12, G16] | §Common Pitfalls Pitfall 2-3, §Code Examples |
| CONF-03 | Asymmetric conformal uses α/2 and 1−α/2 quantiles of signed residuals; simulated coverage within ±2pp of nominal [G13] | §Common Pitfalls Pitfall 4, §Code Examples |
| CONF-04 | Adaptive conformal uses normalised scores \|r\|/d at calibration and d-scaling at prediction, independent of batch composition [G14] | §Common Pitfalls Pitfall 5, §Code Examples |
| CONF-05 | "JackknifePlus" either implements jackknife+ or is renamed/removed [G15] | §Common Pitfalls Pitfall 6 |
</phase_requirements>

## Summary

The conformal/bootstrap surface splits cleanly into two layers that must be fixed independently: an **SQL macro layer** (`src/macros/ts_macros.cpp`) that currently destroys horizon ordering and pools calibration across groups, and a **Rust core layer** (`crates/anofox-fcst-core/src/conformal.rs`, `bootstrap.rs`) that has four distinct, independently-verified statistical bugs (finite-sample clamping, wrong asymmetric quantile target, no calibration-time difficulty normalisation for adaptive, and a cosmetic "JackknifePlus" that is bit-identical to split). The bootstrap sign bug is in the single line where this crate's wrapper calls the upstream `anofox_forecast::postprocess::BootstrapPredictor::fit(forecasts, actuals)` with arguments swapped relative to the sign convention (`actual − forecast`) used everywhere else in this codebase.

All four Rust bugs are `[VERIFIED]` by direct inspection, each confirming a 1:1 match with the STAT-AUDIT-2026-10-08.md finding IDs: G12 (clamp sites at `conformal_quantile`:133-139, and the `conformal_learn`/`conformal_apply` duplicates of the same formula at lines 773-774, 788-789, 821-822, 830-831, 844-845), G13 (asymmetric quantile target at lines 390-410 and its `conformal_learn` duplicate at 813-836), G14 (`conformal_predict_adaptive` at lines 301-349 never receives a calibration-time difficulty array — its signature has no such parameter — and `conformal_apply`'s adaptive branch at lines 932-938 normalises the *prediction-time* difficulty by its own batch mean), G15 (`conformal_learn`'s `JackknifePlus` strategy at lines 765-779 and `conformal_apply`'s jackknife branch at lines 941-983 compute the identical `⌈(n+1)(1−α)⌉/n` quantile of the full residual pool that `Split` uses — no leave-one-out refit exists anywhere in this crate), and G16 (the upstream panic site is `anofox-forecast-0.15.10`'s `ConformalPredictor::compute_quantile` at `postprocess/conformal.rs:475`, `scores.sort_by(|a,b| a.partial_cmp(b).unwrap())` — this crate's own `split_quantile` at `conformal.rs:1958` deliberately bit-replicates that same unguarded `.unwrap()` to match it, per the comment at lines 1950-1955; the fix must filter non-finite residuals in this crate's wrapper code, not in the vendored/pinned upstream crate, since Phase 16 does not touch the anofox-forecast dependency).

The bootstrap sign bug (G2) is `[VERIFIED]` by reading upstream's own doc comment: `BootstrapPredictor::fit(forecasts, actuals)` "Computes residuals (forecast − actual)" (`postprocess/bootstrap.rs:90-104`). This crate's `bootstrap_intervals`/`bootstrap_quantiles` call `predictor.fit(&zeros, residuals)` (forecasts=zeros, actuals=residuals_input) at `bootstrap.rs:74-75` and `:135-136`, producing an internal residual of `0 − residuals_input = -residuals_input` — the exact negation of this codebase's `actual − forecast` convention (confirmed at `ts_macros.cpp:1730`: `(actual_col - forecast_col)::DOUBLE AS residual`). The fix is a one-line argument swap at each call site: `predictor.fit(residuals, &zeros)`.

The SQL macro layer's D4/D5/D6 bugs are all `[VERIFIED]` at exact line numbers in `src/macros/ts_macros.cpp` (current HEAD, post-Phase-12 renumbering — the audit's own line numbers for D4-D6 are stale per the audit's own note and per CONTEXT.md). Useful news for planning: the FFI layer *already* has most of the low-level plumbing Phase 16 needs — `anofox_ts_conformal_coverage`/`conformal_coverage()` (empirical coverage, already exists), `anofox_ts_conformal_evaluate` (coverage + violation_rate + mean_width + winkler, already exists), and `conformal_learn`/`conformal_apply`/`conformalize` already thread `Option<&[f64]>` difficulty parameters through both calibration and prediction (the FFI signatures for `anofox_ts_conformal_learn`/`anofox_ts_conformal_apply` already accept a `difficulty` pointer — currently always passed as `nullptr` from the C++ layer). This means plan 2's Rust work is mostly *fixing the math inside* existing functions/call sites, not inventing new FFI surface — except for CONF-04's calibration-time difficulty, which needs a genuinely new parameter on `conformal_predict_adaptive`'s legacy single-step API (the learn/apply v2 API's signature already has the plumbing, just not the correct math).

**Primary recommendation:** Treat the three plans as: (1) rewrite the 4 SQL macros to thread a step/date column and per-group calibration through `GROUP BY ... ORDER BY <step_col>` instead of value-sorted `LIST()`, reusing the already-correct `anofox_ts_conformal_predict_per_step` pattern as a model for "ordered pairing is a Rust-validated invariant, not a SQL convention"; (2) fix the four Rust math bugs in-place (clamp→NULL+flag, asymmetric signed-residual quantiles, adaptive calibration-time normalisation, remove JackknifePlus) with a new `crates/anofox-fcst-core/tests/conformal_reference.rs` carrying seeded Monte Carlo coverage assertions against independent numpy references; (3) fix the one-line bootstrap sign swap, delete JackknifePlus's SQL-visible name entirely (hard error on all 4 aliases), and sweep CHANGELOG/docs/skill text that currently documents the old (broken) behavior as a known caveat.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Residual computation (`actual − forecast`) | Rust (`anofox-fcst-core`) | — | D-02/D-06 lock this; SQL currently computes it (`ts_macros.cpp:1730`, `:1791`), must move to Rust via validity-passed list pairs |
| Horizon ordering / pairing | SQL macro (GROUP BY + `ORDER BY <step_col>` inside `LIST()`) | — | DuckDB owns row ordering before handing arrays to Rust; Rust cannot recover order DuckDB didn't preserve. This is the established pattern already used correctly by `ts_cv_forecast_by`/per-step conformal |
| Quantile / conformity-score math (clamp, asymmetric target, adaptive normalisation, jackknife+) | Rust (`anofox-fcst-core/src/conformal.rs`) | — | Project rule (D-04, CLAUDE.md): "no statistical arithmetic... beyond documented marshalling" in C++/SQL |
| Empirical coverage / evaluation metrics | Rust (`conformal_coverage`, `conformal_evaluate` — already exist) | — | Already implemented; SQL macros just need to call them instead of hardcoding `1 - alpha` |
| Per-group calibration dispatch | SQL macro (`GROUP BY group_col`) | Rust (quantile math per group's residual array) | DuckDB's native GROUP BY is the project's only parallelism primitive (D-04); Rust receives one group's residuals at a time via the scalar function |
| Bootstrap residual resampling | Rust (`anofox-fcst-core/src/bootstrap.rs` wrapping upstream `BootstrapPredictor`) | — | Pure numerical simulation, no SQL involvement beyond marshalling arrays in/out |
| Error surfacing (bad alpha, removed strategy) | C++ scalar function (`throw InvalidInputException` on `error.code == INVALID_INPUT`) | Rust (produces the `AnofoxError` with that code) | Established pattern at `ts_forecast_native.cpp:733-734`; current conformal.cpp discards all errors into silent `NULL` and must be changed to inspect `error.code` |

## Standard Stack

No new external dependencies are required. This phase works entirely within the existing stack:

| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|---------------|
| `anofox-forecast` | 0.15.10 (pinned, unchanged this phase) `[VERIFIED: Cargo.toml:18]` | Supplies `postprocess::BootstrapPredictor`, `postprocess::ConformalPredictor` that this crate wraps | Already the project's forecasting backend; Phase 16 does not bump it (upstream fixes are Phase 11/17's scope per STATE.md) |
| `numpy`/`scipy` | numpy==2.5.3, scipy==1.18.1 `[VERIFIED: validation/reference/python/pyproject.toml:7-8]` | Independent reference implementations for split/asymmetric/adaptive quantiles and Monte Carlo coverage simulation | Already the project's reference-fixture stack (TEST-01); pure quantile/order-statistic math needs no specialized stats package beyond numpy |

**No installation needed** — `validation/reference/python`'s `uv.lock` already pins these versions; a new generator script (`gen_conformal_reference.py`) is added to the existing project, not a new one.

## Package Legitimacy Audit

Not applicable — this phase adds no new external package dependencies (Rust or Python). All work is within already-vetted, already-pinned crates (`anofox-forecast` 0.15.10) and the already-pinned `validation/reference/python` uv project.

## SQL Macro Audit (RUST-04 — exact current state)

All four macros below are read from `src/macros/ts_macros.cpp` at current HEAD (these line numbers are fresh this session — the audit's own D4-D6 numbers are stale per CONTEXT.md's note that "Phase 12 shifted them").

### `ts_conformal_by` — `ts_macros.cpp:1720-1775`
```sql
-- current (buggy) residual computation, lines 1727-1733:
residuals AS (
    SELECT group_col AS _grp, (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
    WHERE actual_col IS NOT NULL AND forecast_col IS NOT NULL
),
-- current (buggy) VALUE-ordered forecast list, lines 1734-1741:
point_forecasts AS (
    SELECT group_col AS _grp,
        LIST(point_forecast_col::DOUBLE ORDER BY point_forecast_col) AS _forecasts
    FROM query_table(backtest_results::VARCHAR)
    WHERE point_forecast_col IS NOT NULL
    GROUP BY group_col
),
```
Params MAP today only supports `alpha` and `method` (`'symmetric'`/`'asymmetric'`) — no `'adaptive'` branch exists in the `CASE` at lines 1745-1758, and no step-column parameter exists in the signature `{"backtest_results", "group_col", "actual_col", "forecast_col", "point_forecast_col", "params", nullptr}` (line 1720). D-01 requires adding a step/date column parameter; D-08 requires an `'adaptive'` branch plus a difficulty-column parameter.

Output today (`SELECT` at 1763-1771): `group_col, point, lower, upper, coverage, conformity_score, method`. D-02 requires `coverage` to become empirical (call `ts_conformal_coverage`/`ts_conformal_evaluate` after applying, not the Rust struct's own nominal-coverage field) and a new `nominal_level` column. D-06 requires a new `n_calibration` column.

### `ts_conformal_calibrate` — `ts_macros.cpp:1783-1803`
```sql
-- line 1783: NO group_col parameter at all — pools every group into ONE row
{"ts_conformal_calibrate", {"backtest_results", "actual_col", "forecast_col", "params", nullptr}, ...}
-- line 1796-1798:
SELECT
    ts_conformal_quantile(LIST(residual), (SELECT _alpha FROM _params)) AS conformity_score,
    1.0 - (SELECT _alpha FROM _params) AS coverage,   -- nominal, not empirical (D6)
    COUNT(*)::BIGINT AS n_residuals
FROM residuals
```
D-03 requires a required `group_col` parameter (or, per Claude's Discretion, promoting this to a `_by`-named function) with `GROUP BY group_col`, defaulting to per-group calibration, with `params := {'pooled': true}` as the explicit opt-out that drops the `GROUP BY`.

### `ts_conformal_apply_by` — `ts_macros.cpp:1810-1831`
```sql
-- lines 1815-1821: VALUE-ordered forecast list (same D4 bug as ts_conformal_by)
ts_conformal_intervals(
    LIST(forecast_col::DOUBLE ORDER BY forecast_col),
    conformity_score::DOUBLE
) AS interval_result
```
Signature `{"forecast_results", "group_col", "forecast_col", "conformity_score", nullptr}` (line 1810) has no step column. D-01 applies identically to this macro.

### `ts_interval_width_by` — `ts_macros.cpp:1836-1851`
```sql
-- lines 1840-1843: lower and upper sorted INDEPENDENTLY — row i's lower may not
-- be row i's upper (D5, the most severe of the four bugs: silently wrong pairing,
-- not just wrong order)
ts_mean_interval_width(
    LIST(lower_col::DOUBLE ORDER BY lower_col),
    LIST(upper_col::DOUBLE ORDER BY upper_col)
)
```
Fix requires a single shared `ORDER BY <step_col>` applied to both `LIST()` calls so the pairing is preserved; no Rust change needed here (the Rust `mean_interval_width` operates elementwise on whatever pairs it receives).

### Already-correct reference pattern: per-step conformal
`anofox_ts_conformal_predict_per_step` / `conformal_predict_per_step_validated` (`conformal.rs:1966-` ) is the one conformal entry point that already does horizon-ordering correctly: it takes FLAT, fold-major arrays with an explicit `horizon` parameter, validates `len % horizon == 0` in Rust (not by silent truncation), and is pairwise-complete per step. This function is **not exposed through any SQL macro today** (`grep` for `conformal_predict_per_step` in `ts_macros.cpp` returns nothing) — only as a raw scalar function consumed directly over `LIST(... ORDER BY date)` expressions in hand-written SQL. It is a useful design reference for plan 1 ("what does 'Rust validates pairing, SQL just orders' look like when done right") but is out of this phase's four named functions.

## Architecture Patterns

### System Architecture Diagram

```
                    SQL layer (ts_macros.cpp)
  ┌─────────────────────────────────────────────────────────┐
  │ ts_conformal_by(tbl, group, actual, forecast, point,     │
  │                 step_col, params)                        │
  │   │                                                       │
  │   ├─ GROUP BY group_col                                   │
  │   │    └─ LIST(actual ORDER BY step_col)  ──┐             │
  │   │    └─ LIST(forecast ORDER BY step_col) ─┤  paired,    │
  │   │    └─ LIST(point ORDER BY step_col)  ───┘  ordered    │
  │   │         arrays, one row per group                     │
  │   ▼                                                        │
  │ scalar fn: ts_conformal_predict{,_asymmetric,_adaptive}    │
  │   (conformal.cpp)                                          │
  │   │  - ListDoubleReader unpacks LIST → Vec<f64> + validity │
  │   │  - calls into Rust via FFI                             │
  │   ▼                                                        │
  └─────────────────────────────────────────────────────────┘
                    Rust core (conformal.rs)
  ┌─────────────────────────────────────────────────────────┐
  │ conformal_quantile(residuals, alpha)                      │
  │   - filter non-finite BEFORE sort (G16 fix)                │
  │   - compute ⌈(n+1)(1-α)⌉/n; if > n → Err/sentinel, NOT     │
  │     clamp(0,1) (G12 fix, D-05)                             │
  │                                                             │
  │ conformal_predict_asymmetric(residuals, forecasts, alpha)  │
  │   - SIGNED residuals; quantile targets α/2 and 1-α/2        │
  │     of the SAME signed array (G13 fix, D-07)                │
  │                                                             │
  │ conformal_predict_adaptive(residuals, forecasts,            │
  │     difficulty_cal, difficulty_pred, alpha)                 │
  │   - scores = |residuals_i| / difficulty_cal_i (NEW param)   │
  │   - quantile q over normalized scores                       │
  │   - interval_i = forecast_i ± q * difficulty_pred_i         │
  │     (no batch-mean renormalization, G14 fix, D-08)           │
  │                                                             │
  │ conformal_coverage(actuals, lower, upper)  [ALREADY EXISTS] │
  │ conformal_evaluate(...)                    [ALREADY EXISTS] │
  └─────────────────────────────────────────────────────────┘
                    FFI boundary (lib.rs)
  ┌─────────────────────────────────────────────────────────┐
  │ error.code == INVALID_INPUT  → C++ throws                  │
  │   InvalidInputException (D-06: bad alpha)                  │
  │ error.code == INSUFFICIENT_DATA → NOT thrown; Rust instead  │
  │   returns Ok(sentinel) carrying calibration_insufficient    │
  │   flag + n_calibration (D-05)                               │
  └─────────────────────────────────────────────────────────┘
```

### Recommended Project Structure (no new files beyond tests/fixtures)
```
crates/anofox-fcst-core/src/
├── conformal.rs         # fix in place: quantile clamp, asymmetric target,
│                         # adaptive calibration difficulty, remove JackknifePlus
├── bootstrap.rs          # fix in place: swap fit() argument order (2 call sites)
crates/anofox-fcst-core/tests/
├── conformal_reference.rs   # NEW — Monte Carlo coverage tests (D-11)
├── common/mod.rs            # reuse existing load_fixture() (no change)
validation/reference/python/
├── gen_conformal_reference.py  # NEW — independent numpy split/asymmetric/
│                                 # adaptive quantile + bootstrap-sign reference
validation/reference/fixtures/
├── conformal/cases.json        # NEW fixture family
src/macros/ts_macros.cpp         # rewrite 4 macro bodies (D-01..D-03)
src/scalar_functions/conformal.cpp  # add error.code inspection + throw (D-06);
│                                     # thread difficulty params (D-08); remove
│                                     # JackknifePlus strategy parsing (D-09)
src/scalar_functions/bootstrap.cpp  # no change needed (just calls Rust)
test/sql/ts_conformal_reference.test  # NEW — non-json-gated SQL assertions (D-11)
```

### Pattern 1: Per-group, step-ordered LIST construction (fixes D-01, D-03)
**What:** Every `LIST(col ORDER BY ...)` inside a conformal macro must order by the *same* explicit step/date column, and that column must be a required macro parameter, not inferred from value.
**When to use:** Any macro building parallel arrays (actual, forecast, point, lower, upper) that Rust will treat as index-aligned.
**Example (illustrative — this phase's actual macro, not yet written):**
```sql
-- Source: this codebase's own ts_cv_forecast_by pattern (src/macros/ts_macros.cpp),
-- adapted to the conformal macros per D-01.
WITH residuals AS (
    SELECT group_col AS _grp, step_col AS _step,
           (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
),
per_group AS (
    SELECT _grp,
        LIST(residual ORDER BY _step) AS _residuals,
        LIST(point_forecast_col ORDER BY _step) AS _forecasts
    FROM residuals
    GROUP BY _grp
)
```
`[ASSUMED]` — exact macro body is this phase's deliverable, not yet written; shown to illustrate the required `ORDER BY _step` discipline applied consistently to every `LIST()` in the macro.

### Pattern 2: Rust-validated pairing, not SQL-assumed pairing
**What:** `anofox_ts_conformal_predict_per_step` (`src/include/anofox_fcst_ffi.h:3327`) validates `fold_forecasts_len % horizon == 0` in Rust rather than trusting the caller's row count — "the caller never computes `n_folds` itself" per its own doc comment at `anofox_fcst_ffi.h:3312-3326`.
**When to use:** Model for how the fixed macros should delegate length/alignment invariants to Rust rather than re-deriving them in SQL.
**Example:**
```c
// Source: src/include/anofox_fcst_ffi.h:3327-3339 [VERIFIED: read this session]
bool anofox_ts_conformal_predict_per_step(const double *fold_forecasts,
                                          const uint64_t *fold_forecasts_validity,
                                          size_t fold_forecasts_len,
                                          const double *fold_actuals,
                                          const uint64_t *fold_actuals_validity,
                                          size_t fold_actuals_len,
                                          size_t horizon,
                                          const double *point_forecasts,
                                          const uint64_t *point_validity,
                                          size_t n_point_forecasts,
                                          double alpha,
                                          struct ConformalPerStepResultFFI *out_result,
                                          struct AnofoxError *out_error);
```

### Pattern 3: Error-code-gated throw vs. silent NULL (fixes D-06)
**What:** Established elsewhere in the codebase — inspect `AnofoxError.code` after a failed FFI call and `throw InvalidInputException` for genuinely invalid input, vs. `SetNull` for legitimately-missing-data cases.
**When to use:** Any conformal/bootstrap scalar function receiving a user-supplied `alpha` or `method`/`strategy` string.
**Example:**
```cpp
// Source: src/table_functions/ts_forecast_native.cpp:733-734 [VERIFIED: read this session]
if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
    throw InvalidInputException(string(error.message));
}
```
**Current conformal.cpp behavior that must change:** every `TsConformal*Function` in `src/scalar_functions/conformal.cpp` currently does, on `!success`, only `FlatVector::SetNull(result, row_idx, true); continue;` — discarding `error.code`/`error.message` entirely (confirmed at lines 251-254, 373-376, 544-547, 710-713, 842-845, 952-955). D-06 requires at least the bad-`alpha` case to throw instead.

### Anti-Patterns to Avoid
- **Clamping a quantile level to `[0,1]` when it exceeds 1 (G12):** `((n+1)(1-α)).ceil()/n` can exceed 1 whenever `n` is small relative to `1-α` (e.g. `n=5, α=0.1` → `⌈5.4⌉/5 = 6/5 = 1.2`). `conformal_quantile`'s `.clamp(0.0, 1.0)` at `conformal.rs:139` then takes the quantile at level 1.0, i.e. the maximum residual — silently *understating* the true required interval (the correct theoretical answer is that no finite-sample conformal guarantee exists at that α/n and the interval should be reported unbounded/NULL, per D-05). This exact pattern is duplicated at 5 more sites (`conformal_predict_asymmetric`:399,409; `conformal_learn`:774,789,845).
- **Splitting residuals into positive/negative subsets and taking the SAME `(1-α/2)` quantile of each subset (G13):** This is not what CQR-style asymmetric conformal (Romano, Patterson & Candès 2019) does. The correct construction uses the α/2 and 1−α/2 quantiles of the *full signed* residual array (one sorted array, two cut points), not two independently-sorted one-sided subsets each queried at the same high quantile. Taking `(1-α/2)` of only the positive residuals (a strictly smaller, more extreme subset than the full array) produces margins that are systematically too wide — matching the audit's empirical finding of "~40% too wide."
- **Normalizing a difficulty score by the mean of the *current prediction batch*, not the calibration batch (G14):** `conformal_apply`'s adaptive branch (`conformal.rs:932-938`) computes `mean_d` from whatever `difficulty` array is passed *at apply time* — meaning the same single-row prediction, called with different co-predicted batches, gets different interval widths purely from batch composition, which breaks the per-row independence conformal prediction is supposed to guarantee.
- **Giving a different calibration strategy an identical quantile formula and calling it a different name (G15):** `JackknifePlus`'s "distinguishing" code path (`conformal.rs:941-983`) computes exactly `⌈(n+1)(1-α)⌉/n` over the full residual pool — bit-for-bit the same math as `Split`. A real jackknife+ (Barber, Candès, Ramdas & Tibshirani 2021) requires n leave-one-out model refits and taking min/max over LOO-adjusted residuals, which this GROUP BY/scalar architecture cannot produce without a refit capability (correctly deferred per D-09).
- **Letting an upstream library's unguarded `.unwrap()` dictate this crate's NaN policy (G16):** `conformal.rs:1958`'s `split_quantile` is a deliberate bit-replica of upstream `ConformalPredictor::compute_quantile`'s `scores.sort_by(|a,b| a.partial_cmp(b).unwrap())` (`anofox-forecast-0.15.10/src/postprocess/conformal.rs:475`) — fixing the NaN filter must happen in *this* crate's wrapper code before any residual reaches that function, not by waiting on an upstream crate bump (which is explicitly out of scope — Phase 16 does not touch `anofox-forecast`'s version).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Empirical coverage computation | A new SQL `AVG(CASE WHEN actual BETWEEN lower AND upper ...)` expression | `ts_conformal_coverage` / `conformal_coverage()` (`conformal.rs:1069-1091`, FFI `anofox_ts_conformal_coverage` — already exists, already registered) | Rust-owned per D-02/project rule; already correct and already tested |
| Interval-quality metrics (violation rate, Winkler score) | New SQL formulas | `ts_conformal_evaluate` / `conformal_evaluate()` (FFI `anofox_ts_conformal_evaluate` — already exists) | Already implements Winkler score correctly per the audit's "Verified correct" list |
| Quantile-with-linear-interpolation | A new interpolation helper | `compute_quantile` (`conformal.rs:429-448`) — already correct, reuse it for the fixed asymmetric/adaptive paths | The interpolation itself (type-7-like) is not flagged by the audit as buggy; only the *quantile level fed into it* is wrong in 3 of 4 methods |
| Monte Carlo coverage simulation harness | A bespoke Rust `rand`-based simulation inline in the test | Follow the existing `bootstrap.rs` test pattern (`#[test] fn test_bootstrap_intervals_basic`, seeded via `Some(42)`) and the project's `validation/reference/python` uv project for the independent numpy cross-check | Consistency with the established fixture-contract pattern (TEST-01); avoids re-deriving provenance/versioning conventions |

**Key insight:** All of the hard infrastructure (empirical coverage, Winkler score, per-step pairwise-complete validation, FFI difficulty threading) already exists in this crate. Phase 16's Rust work is almost entirely *fixing the formula inside* 4 existing functions and *wiring already-existing* functions into the SQL macros — not building new machinery.

## Common Pitfalls

### Pitfall 1: Bootstrap residual sign (G2, CONF-01)
**What goes wrong:** Right-skewed (e.g. log-normal) residuals produce bootstrap intervals whose long tail sits on the *lower* side of the point forecast instead of the upper side.
**Why it happens:** `bootstrap_intervals`/`bootstrap_quantiles` (`bootstrap.rs:74-75`, `:135-136`) call upstream `BootstrapPredictor::fit(forecasts, actuals)` with `forecasts = zeros`, `actuals = residuals_input`. Upstream's own doc comment (`anofox-forecast-0.15.10/src/postprocess/bootstrap.rs:90`) states it "computes residuals (forecast − actual)" — so the internal residual used for resampling is `0 − residuals_input = -residuals_input`, the exact negation of the `actual − forecast` sign convention used everywhere else in this codebase (confirmed convention at `ts_macros.cpp:1730`).
**How to avoid:** Swap the argument order: `predictor.fit(residuals, &zeros)` (so internal residual = `residuals_input − 0 = residuals_input`, correct sign) at both call sites.
**Warning signs:** A seeded log-normal-residual fixture (long right tail) produces an interval whose `lower` bound is further from the point forecast than its `upper` bound — i.e. mirrored from what the actual skew of the input residuals implies. `[VERIFIED: crates/anofox-fcst-core/src/bootstrap.rs:74-75, :90-104(upstream postprocess/bootstrap.rs); verbatim upstream doc "Computes residuals (forecast - actual)" at postprocess/bootstrap.rs:90]`

### Pitfall 2: Finite-sample quantile clamp instead of unbounded/NULL (G12, CONF-02, D-05)
**What goes wrong:** When the calibration set is too small for the requested coverage (`⌈(n+1)(1−α)⌉ > n`), the implementation silently clamps the quantile level to 1.0 and returns the maximum observed residual as the conformity score — a finite, plausible-looking number that gives false confidence, rather than correctly reporting "no finite-sample guarantee is possible at this n/α."
**Why it happens:** `conformal_quantile` (`conformal.rs:133-139`) computes `quantile_level = ((n+1.0)*(1.0-alpha)).ceil()/n` then unconditionally `.clamp(0.0, 1.0)`s it before passing to `compute_quantile`, which at `quantile >= 1.0` returns `sorted_data[n-1]` (the max) — never erroring or flagging insufficiency. The identical pattern recurs in `conformal_learn`'s Symmetric (788-789), JackknifePlus (773-774), and both Asymmetric branches (821-822, 830-831), and in `conformal_predict_asymmetric` (399, 409).
**How to avoid:** Before clamping, check `quantile_level > 1.0` and short-circuit to the D-05 behavior: NULL lower/upper/width plus a `calibration_insufficient = true` flag (or equivalently, surface `n_calibration` so the SQL layer can derive the same flag) — never silently substitute the max.
**Warning signs:** A calibration set with `n` small enough that `⌈(n+1)(1-α)⌉ > n` (e.g. n=5, α=0.1) still returns a non-NULL, finite interval today. `[VERIFIED: crates/anofox-fcst-core/src/conformal.rs:133-139, 773-774, 788-789, 821-822, 830-831, 844-845, 399, 409 — all read this session]`

### Pitfall 3: Unguarded `.unwrap()` on NaN in a bit-replicated upstream quantile path (G16, CONF-02, D-06)
**What goes wrong:** A residual that is numerically `NaN` (not NULL/missing by validity bitmap, but a *valid* f64 that happens to be NaN — e.g. produced upstream by a 0/0 division) reaching `split_quantile` panics the whole process via `sorted.sort_by(|a,b| a.partial_cmp(b).unwrap())` (`conformal.rs:1958`), which the FFI's `catch_unwind` converts to an `AnofoxError{code: PANIC_CAUGHT}` — but only after the panic has already happened, which is fragile and loses context.
**Why it happens:** This function deliberately bit-replicates upstream `ConformalPredictor::compute_quantile`'s identical unguarded `.unwrap()` (confirmed at `anofox-forecast-0.15.10/src/postprocess/conformal.rs:475`) to stay bitwise-identical to upstream's per-step result for validation purposes (per the doc comment at `conformal.rs:1950-1955`). Note: the other, non-bit-replicated sort sites in this crate (lines 133, 396, 406, 758, 809, 811) already use the safe `.unwrap_or(std::cmp::Ordering::Equal)` fallback — the panic risk is isolated to `split_quantile` at line 1958 and whatever call path feeds it unfiltered data.
**How to avoid:** Filter non-finite (`!is_finite()`) residuals out of the input to `split_quantile`/any quantile path *before* calling it, in this crate's own code — not by editing the pinned/vendored upstream crate (out of scope this phase). D-06 explicitly calls for "NaN/NULL residuals... filtered before quantiles are taken."
**Warning signs:** `[VERIFIED: crates/anofox-fcst-core/src/conformal.rs:1950-1964 (doc comment + code), anofox-forecast-0.15.10/src/postprocess/conformal.rs:473-475 (confirmed via `grep` + `Read` of the cached crate source this session)]`

### Pitfall 4: Asymmetric conformal's quantile *target*, not its split logic, is wrong (G13, CONF-03, D-07)
**What goes wrong:** The current implementation computes a margin roughly 40% too wide (per the audit's empirical measurement), because it queries the *same* high quantile level `(1-α/2)` independently on both the positive-only and negative-only (absolute-valued) residual subsets, instead of taking the α/2 and (1-α/2) quantiles of the one combined *signed* residual array.
**Why it happens:** `conformal_predict_asymmetric` (`conformal.rs:380-410`) and `conformal_learn`'s Asymmetric branch (`conformal.rs:796-836`) both: (1) split `residuals` into `positive` (values > 0) and `negative` (abs of values < 0), (2) independently sort each, (3) compute `q = ((n+1)*(1-alpha_half)).ceil()/n` *for each subset's own n* and apply it to that subset. This produces a quantile of a strictly-smaller, same-signed subset at a high percentile — which is not equivalent to the α/2 and 1-α/2 quantiles of the full signed distribution that CQR-style methods (Romano, Patterson & Candès, "Conformalized Quantile Regression", 2019) call for.
**How to avoid:** Compute one sorted array of *signed* residuals; the lower margin is (the negation of) the α/2 quantile of that signed array, and the upper margin is the 1-α/2 quantile of the same signed array — both cut points on one array, with the usual `⌈(n+1)(1-α/2)⌉/n`-style finite-sample correction applied to each cut (n = full residual count, not the subset count).
**Warning signs:** A seeded Monte Carlo test (D-07 requires this) comparing achieved coverage to `1-α` on both symmetric and right-skewed residual fixtures will show coverage well above nominal (interval too wide) under the current formula. `[VERIFIED: crates/anofox-fcst-core/src/conformal.rs:380-410, 796-836 — read this session]` `[CITED: Romano, Patterson & Candès (2019), "Conformalized Quantile Regression", NeurIPS — the α/2 / 1-α/2 signed-quantile construction is the standard CQR formulation]`

### Pitfall 5: Adaptive conformal never learns a calibration-time difficulty (G14, CONF-04, D-08)
**What goes wrong:** The adaptive method is supposed to produce "locally-weighted" intervals (Papadopoulos et al. normalized nonconformity; Lei et al. 2018 locally-weighted conformal) where the calibration quantile is computed over *normalized* scores `|r_i|/d_i` (so a hard-to-predict calibration point doesn't dominate the global quantile), and the per-row interval at prediction time is `q · d_new` using that one fixed `q`. Instead, this implementation computes the SAME unweighted base quantile as symmetric conformal for calibration, and only applies difficulty scaling at prediction time — and even then, re-normalizes the prediction-time difficulty by its own batch mean, making the resulting interval width depend on which other rows happen to be in the same prediction call.
**Why it happens:** `conformal_predict_adaptive`'s legacy single-step signature (`conformal.rs:301-306`) has no calibration-time difficulty parameter at all — only `residuals`, `forecasts`, `difficulty` (prediction-time only), `alpha`; its `base_score = conformal_quantile(residuals, alpha)` (line 323) is computed from raw, unweighted residuals. The v2 `conformal_learn`'s Adaptive branch (`conformal.rs:839-850`) does the same thing — "we store the base symmetric scores; the actual scaling happens at apply time" (comment at line 840-841). At apply time, `conformal_apply`'s `normalized_difficulty` (lines 932-938) computes `mean_d` from whatever `difficulty` slice the *current call* passes, so the normalization — and hence interval width per row — depends on co-predicted batch composition, violating per-row independence.
**How to avoid:** Per D-08: at calibration, require a `difficulty` array matching the residuals (this is a NEW required parameter on the legacy `conformal_predict_adaptive` signature; the v2 `conformal_learn` already has an `Option<&[f64]>` difficulty parameter but doesn't use it for Adaptive — wire it in), compute scores `s_i = |r_i| / d_i`, take the usual finite-sample quantile `q` of `s_i` (not of raw `|r_i|`). At prediction, for each new forecast with its own difficulty `d_j`, the interval is `forecast_j ± q · d_j` — no batch-mean renormalization of `d_j` at all (prediction-time difficulty is used as-is, unnormalized).
**Warning signs:** Calling the same adaptive-apply function twice with the same single row but different co-predicted batches produces different interval widths for that row. `[VERIFIED: crates/anofox-fcst-core/src/conformal.rs:301-306, 315-349, 839-850, 932-938, 1000-1014 — read this session]` `[CITED: Papadopoulos et al. (2008) "Normalized Nonconformity Measures for Regression Conformal Prediction"; Lei, G'Sell, Rinaldo, Tibshirani & Wasserman (2018) "Distribution-Free Predictive Inference for Regression", JASA — both describe the normalize-at-calibration / scale-at-prediction construction]`

### Pitfall 6: "JackknifePlus" is cosmetically distinct but mathematically identical to Split (G15, CONF-05, D-09)
**What goes wrong:** Choosing `strategy := 'jackknife_plus'` (or any of its 4 aliases) implies the stronger leave-one-out robustness guarantee of true jackknife+ (Barber, Candès, Ramdas & Tibshirani, "Predictive inference with the jackknife+", Annals of Statistics 2021), but silently delivers bit-identical results to `'split'`.
**Why it happens:** `conformal_learn`'s `JackknifePlus` branch (`conformal.rs:765-779`) stores the full sorted abs-residual array and computes `scores_lower`/`scores_upper` via the exact same `⌈(n+1)(1-α)⌉/n` formula as Split. `conformal_apply`'s jackknife branch (`conformal.rs:941-983`) re-derives the same quantile at apply time from the stored residuals — again the identical formula, never performing a leave-one-out refit (which this GROUP BY/scalar architecture structurally cannot do without a model-refit capability it doesn't have).
**How to avoid:** Per D-09, remove `'JackknifePlus'`/`'jackknife+'`/`'jackknifeplus'`/`'jackknife_plus'`/`'jk+'` as valid strategy values entirely — `ConformalStrategy::from_str` (`conformal.rs:521-528`) and `ParseConformalStrategy` (`src/scalar_functions/conformal.cpp:464-473`) must raise an error (not silently fall back to Split as `ParseConformalStrategy` currently does — confirmed: it returns `SPLIT` for any unrecognized string, including typos, which is itself an independent minor gap the audit calls out as "Unrecognized strategy strings silently fall back to 'split'" in `docs/api/11-conformal-prediction.md`'s Coverage caveats section) explaining it was split conformal and pointing users to `'split'`/`'crossval'`.
**Warning signs:** `[VERIFIED: crates/anofox-fcst-core/src/conformal.rs:488-529 (ConformalStrategy enum + FromStr), :765-779, :941-983 — read this session; src/scalar_functions/conformal.cpp:464-473, 489-490 (ParseConformalStrategy silent-default-to-SPLIT) — read this session]`

## Code Examples

### Bootstrap sign fix (CONF-01, D-10)
```rust
// Source: crates/anofox-fcst-core/src/bootstrap.rs:73-76 (current, buggy)
let zeros: Vec<f64> = vec![0.0; residuals.len()];
let result = predictor
    .fit(&zeros, residuals)   // BUG: internal residual = 0 - residuals[i] = -residuals[i]
    .map_err(...)?;

// Fix: swap argument order so internal residual = residuals[i] - 0 = residuals[i]
let result = predictor
    .fit(residuals, &zeros)
    .map_err(...)?;
```
Apply the identical swap at `bootstrap_quantiles` (`bootstrap.rs:135-136`, same pattern).

### Finite-sample insufficiency as a sentinel, not a clamp (CONF-02, D-05)
```rust
// Source: crates/anofox-fcst-core/src/conformal.rs:133-139 (current, buggy)
let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
let quantile_level = quantile_level.clamp(0.0, 1.0);   // BUG: silently substitutes max residual
let quantile = compute_quantile(&abs_residuals, quantile_level);
Ok(quantile)

// Fix sketch (illustrative; exact Result/sentinel type is this phase's design, [ASSUMED]):
let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
if quantile_level > 1.0 {
    // D-05: NULL bounds + calibration_insufficient flag, n_calibration reported —
    // caller-visible via a result variant/field, not a silent substitution.
    return Ok(ConformalQuantileResult::insufficient(n as usize));
}
let quantile = compute_quantile(&abs_residuals, quantile_level);
```

### Error-code-gated throw for bad alpha (CONF-02, D-06)
```cpp
// Source: src/table_functions/ts_forecast_native.cpp:733-734 [VERIFIED pattern,
// read this session] — apply the same pattern in conformal.cpp's current
// silent-NULL sites (e.g. TsConformalPredictFunction, conformal.cpp:251-254):
if (!success) {
    if (error.code == INVALID_INPUT) {
        throw InvalidInputException(string(error.message));  // D-06: alpha not in (0,1)
    }
    FlatVector::SetNull(result, row_idx, true);  // keep NULL for non-error cases (e.g. D-05 insufficiency)
    continue;
}
```

### NaN-filtered quantile input (CONF-02, D-06)
```rust
// Fix sketch for conformal.rs's split_quantile call sites (illustrative, [ASSUMED]
// exact placement — must run in THIS crate before upstream ConformalPredictor
// code, confirmed panic site anofox-forecast-0.15.10/src/postprocess/conformal.rs:475):
let finite_residuals: Vec<f64> = residuals.iter().copied().filter(|r| r.is_finite()).collect();
// ... then feed finite_residuals wherever raw `residuals` was passed to a path
// that reaches split_quantile or conformal_quantile.
```

### Signed-residual asymmetric quantiles (CONF-03, D-07)
```rust
// Fix sketch replacing conformal.rs:380-410's positive/negative-subset logic
// (illustrative; [ASSUMED] exact function shape, grounded in the CQR formulation
// [CITED: Romano, Patterson & Candès 2019]):
let mut signed: Vec<f64> = residuals.to_vec();
signed.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
let n = signed.len() as f64;
let alpha_half = alpha / 2.0;
let lo_level = ((n + 1.0) * alpha_half).ceil() / n;            // lower tail cut
let hi_level = ((n + 1.0) * (1.0 - alpha_half)).ceil() / n;    // upper tail cut
// D-05 applies to EACH cut independently: if either level > 1.0 (or < 0.0 after
// the ceil/n arithmetic), that side's bound is NULL/insufficient — do not clamp.
let lower_margin = compute_quantile(&signed, lo_level.max(0.0));   // signed value, used as-is (can be negative)
let upper_margin = compute_quantile(&signed, hi_level.min(1.0));
let lower: Vec<f64> = forecasts.iter().map(|f| f + lower_margin).collect(); // NOTE: + because lower_margin is itself signed/negative-leaning
let upper: Vec<f64> = forecasts.iter().map(|f| f + upper_margin).collect();
```

### Adaptive calibration-time normalization (CONF-04, D-08)
```rust
// Fix sketch for the NEW calibration-time difficulty path (illustrative, [ASSUMED]
// exact signature — conformal_predict_adaptive's current signature at
// conformal.rs:301-306 has NO calibration difficulty parameter and must gain one):
pub fn conformal_predict_adaptive(
    residuals: &[f64],
    difficulty_cal: &[f64],     // NEW — same length as residuals (D-08)
    forecasts: &[f64],
    difficulty_pred: &[f64],    // renamed from `difficulty`; same length as forecasts
    alpha: f64,
) -> Result<ConformalResult> {
    // scores = |r_i| / d_i  (normalized nonconformity, Papadopoulos et al.)
    let scores: Vec<f64> = residuals.iter().zip(difficulty_cal.iter())
        .map(|(r, d)| r.abs() / d).collect();
    let q = conformal_quantile(&scores, alpha)?;   // reuse the (now-fixed) quantile fn
    // NO batch-mean renormalization of difficulty_pred — use it as-is per row:
    let lower: Vec<f64> = forecasts.iter().zip(difficulty_pred.iter())
        .map(|(f, d)| f - q * d).collect();
    let upper: Vec<f64> = forecasts.iter().zip(difficulty_pred.iter())
        .map(|(f, d)| f + q * d).collect();
    // ...
}
```

### JackknifePlus hard-error (CONF-05, D-09)
```rust
// Source: crates/anofox-fcst-core/src/conformal.rs:518-528 (current FromStr)
"jackknife+" | "jackknifeplus" | "jackknife_plus" | "jk+" => Ok(Self::JackknifePlus),

// Fix: all 4 aliases become a hard, explanatory error (D-09); the JackknifePlus
// variant itself can stay in the enum temporarily only if callers are fully
// blocked from reaching it, or be removed outright — planner's discretion on enum shape:
"jackknife+" | "jackknifeplus" | "jackknife_plus" | "jk+" => Err(ForecastError::InvalidInput(format!(
    "Conformal strategy '{}' is not implemented — this previously behaved like \
     'split' conformal (same quantile formula, no leave-one-out refit) and has been \
     removed to avoid implying a guarantee it didn't provide. Use 'split' or 'crossval'.", s
))),
```
Also fix `ParseConformalStrategy` (`src/scalar_functions/conformal.cpp:464-473`) to stop silently defaulting unrecognized strings to `SPLIT` — any unrecognized strategy string (including the jackknife+ aliases) should surface as a thrown error, not a silent substitution.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `coverage` column = hardcoded `1.0 - alpha` | `coverage` = empirical, computed via `ts_conformal_coverage`/`conformal_evaluate` on the calibration set; new `nominal_level` column carries the requested level | This phase (D-02) | Callers reading `coverage` from `ts_conformal_by`/`ts_conformal_calibrate` output today will see a semantic change — document in CHANGELOG as breaking |
| `LIST(col ORDER BY col)` (value-ordered) | `LIST(col ORDER BY step_col)` (explicit horizon-ordered, required new parameter) | This phase (D-01) | Breaking signature change on 3 macros; every caller must add a step/date column argument |
| `ts_conformal_calibrate` pools all groups | Per-group by default; `params := {'pooled': true}` opts into pooling | This phase (D-03) | Breaking: output goes from 1 row to N rows (one per group) unless pooled is explicitly requested |
| `'jackknife_plus'`/aliases accepted, silently == split | All 4 aliases hard error | This phase (D-09) | Breaking: any caller currently passing these strategy strings must switch to `'split'`/`'crossval'` |

**Deprecated/outdated:**
- `ts_conformal_calibrate`'s nominal `coverage` field: superseded by empirical coverage (D-02).
- `'jackknife_plus'` as a selectable strategy: removed, not deprecated-with-warning — a hard error per D-09 (no compatibility flag per D-11).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Exact Rust function/field names for D-05's "calibration_insufficient" sentinel (e.g. whether it's a new enum variant, an `Option<f64>` width, or a boolean field alongside existing fields) | Code Examples (finite-sample insufficiency sketch) | Low — CONTEXT.md explicitly leaves internal structure of conformal.rs to Claude's Discretion; any shape satisfying "NULL bounds/width + indicator + n reported" meets D-05 |
| A2 | Exact macro parameter names for the new step/date column and difficulty-column parameters | Architecture Patterns Pattern 1; SQL Macro Audit | Low — CONTEXT.md explicitly defers "exact parameter names... consistent with existing `_by` macro naming" to Claude's Discretion |
| A3 | `ts_conformal_calibrate`'s rename/signature shape (new required `group_col` param vs. promotion to a `_by`-suffixed name) | SQL Macro Audit §ts_conformal_calibrate | Low-Medium — either shape satisfies D-03's stated requirement (per-group default, `pooled:true` opt-out); naming consistency is explicitly Claude's Discretion, but a careless choice could complicate the Phase 18 doc/skill sweep |
| A4 | The standard CQR / normalized-adaptive-conformal formulas cited (Romano et al. 2019; Papadopoulos et al. 2008; Lei et al. 2018) are the correct textbook references for D-07/D-08's locked behavior | Common Pitfalls 4-5, Code Examples | Low — these are well-established, widely-cited formulations; CONTEXT.md's own wording ("α/2 and 1−α/2 quantiles of SIGNED residuals", "Scores are \|r\|/d") already specifies the exact target math independent of citation accuracy |
| A5 | Monte Carlo sample-size target: with coverage near 90-95% and a ±2pp margin at 95% confidence, roughly 450-900 independent calibration/test trials (binomial margin-of-error formula `n = z²p(1-p)/e²`, z=1.96, e=0.02) are enough; recommend rounding up to ≥1000-2000 trials for safety margin given CI flakiness tolerance | Execution Flow guidance (sample sizes); not a file section but informs plan 2's test design | Low — this is a standard statistical-power calculation, not implementation-specific; too few trials risks flaky CI, too many just costs test runtime |

**If this table is empty:** N/A — assumptions listed above are all low-risk naming/sizing choices explicitly delegated to Claude's Discretion by CONTEXT.md, not disputed facts about current broken behavior (all of which are `[VERIFIED]` by direct code reading this session).

## Open Questions

1. **Does `ts_conformal_calibrate` need a `_by` rename, or just a new `group_col` parameter?**
   - What we know: D-03 requires per-group-by-default calibration with a `pooled:true` opt-out; the function currently has no group parameter at all (`ts_macros.cpp:1783`).
   - What's unclear: Whether the planner should rename it to match the `_by` convention used by `ts_conformal_apply_by`/`ts_conformal_by`, or keep the name and just insert a required `group_col` parameter (making an un-"_by"-suffixed function behave like one).
   - Recommendation: Keep the name `ts_conformal_calibrate` (avoid a second breaking rename on top of the signature change) and add `group_col` as a new required parameter positioned consistently with the other three macros' `group_col` placement (second positional arg, matching `ts_conformal_by`'s `{backtest_results, group_col, actual_col, ...}` convention) — but this is explicitly Claude's Discretion per CONTEXT.md, so the planner may choose either.

2. **Should the `JackknifePlus`/`jackknife+` enum variant be deleted from `ConformalStrategy`, or kept-but-unreachable?**
   - What we know: D-09 requires the 4 alias strings to hard-error; the `ConformalStrategy` enum variant and its match arms in `conformal_learn`/`conformal_apply` currently implement the (bugged, cosmetic) behavior.
   - What's unclear: Whether removing the enum variant entirely (simplifying match arms, removing dead code) or keeping it as an unreachable variant (in case a future true-jackknife+ implementation wants the same enum slot) is preferred, given the deferred-idea note that true jackknife+ "if a refit-capable architecture is ever added" is explicitly out of scope.
   - Recommendation: Remove the variant entirely — `FromStr` erroring on the alias strings means no code path can construct it, so keeping a permanently-unreachable enum variant only adds dead-code surface; a future true-jackknife+ implementation can re-add the variant when the refit architecture exists.

3. **Where exactly does `n_calibration` get reported — on `ts_conformal_calibrate` only, or on every output row of `ts_conformal_by`/`ts_conformal_apply_by` too?**
   - What we know: D-06 says "the number of residuals used is reported as `n_calibration`"; `ts_conformal_calibrate` already has an analogous `n_residuals` column (`ts_macros.cpp:1798`) that could be renamed/kept-alongside.
   - What's unclear: Whether `ts_conformal_by` (which does calibration *and* application in one call, one row per group) and `ts_conformal_apply_by` (which applies a *pre*-computed score, where "residuals used" refers to the original calibration's `n`, not anything computed in that call) both need this column, or just the calibration-producing functions.
   - Recommendation: Add `n_calibration` to both `ts_conformal_calibrate`'s and `ts_conformal_by`'s output (both actually compute a quantile from residuals in that call); for `ts_conformal_apply_by`, the calibration profile/score passed in would need to already carry `n_calibration` through from when it was computed — if the macro's `conformity_score` parameter stays a bare DOUBLE (not a struct), `ts_conformal_apply_by` cannot report a meaningful `n_calibration` and should omit the column or document that it only applies to the calibration-producing macros.

## Environment Availability

Not applicable — this phase has no new external tool/service dependencies. All work is within the existing Rust/C++/DuckDB toolchain already verified in Phase 9 (MSRV 1.89, DuckDB 1.4.3+, CMake 3.20+) and the existing `validation/reference/python` uv project (already has a committed `uv.lock`).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | `cargo test` (Rust unit/integration), DuckDB's native sqllogictest harness (`.test` files under `test/sql/`) |
| Config file | `crates/anofox-fcst-core/Cargo.toml` (workspace member); no separate test-framework config — tests are plain `#[test]` functions and `.test` sqllogictest files |
| Quick run command | `cargo test -p anofox-fcst-core conformal` (Rust); `make test_release` runs the full SQL suite — no sub-filtering convention observed for individual `.test` files beyond the `make` target |
| Full suite command | `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release && make test_release` (per CONTEXT.md's DoD, consistent with Phases 13-15) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RUST-04 | Horizon pairing preserved; per-group calibration; empirical coverage | SQL (non-json-gated) | `make test_release` (runs `test/sql/ts_conformal_reference.test`) | ❌ Wave 0 — new file |
| CONF-01 | Bootstrap skewed-residual fixture correct-side | Rust unit + numpy cross-check | `cargo test -p anofox-fcst-core bootstrap` | ❌ Wave 0 — extend `bootstrap.rs`'s existing `#[cfg(test)] mod tests` or new `conformal_reference.rs` |
| CONF-02 | Finite-sample insufficiency, bad alpha, NaN filtering | Rust unit | `cargo test -p anofox-fcst-core conformal` | ❌ Wave 0 — new `conformal_reference.rs` |
| CONF-03 | Asymmetric coverage ±2pp Monte Carlo | Rust seeded Monte Carlo | `cargo test -p anofox-fcst-core conformal_reference` | ❌ Wave 0 — new file |
| CONF-04 | Adaptive batch-independence | Rust unit | `cargo test -p anofox-fcst-core conformal_reference` | ❌ Wave 0 — new file |
| CONF-05 | JackknifePlus aliases error | Rust unit + SQL | `cargo test -p anofox-fcst-core conformal::tests` | ❌ Wave 0 — new file |

### Sampling Rate
- **Per task commit:** `cargo test -p anofox-fcst-core conformal bootstrap` + `cargo fmt --check`
- **Per wave merge:** Full `make release && make test_release`
- **Phase gate:** Full suite green before `/gsd-verify-work`, per CONTEXT.md's DoD (cargo fmt --check, cargo test --workspace, RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release, make test_release, snippet harness 0 FAIL for docs)

### Wave 0 Gaps
- [ ] `crates/anofox-fcst-core/tests/conformal_reference.rs` — new file, Monte Carlo coverage tests for CONF-02/03/04/05 (D-11 requirement)
- [ ] `validation/reference/python/gen_conformal_reference.py` — new independent numpy generator for split/asymmetric/adaptive quantiles + bootstrap-sign reference (follow `gen_quality_reference.py`'s structure: `fixture_io.py`'s `generator_path`/`tool_versions`/`write_fixture` helpers)
- [ ] `validation/reference/fixtures/conformal/cases.json` — new fixture family (generated by the above, not hand-written)
- [ ] `test/sql/ts_conformal_reference.test` — new, NON-json-gated (no `require json` line), per D-11
- [ ] Recommended Monte Carlo sample size: ≥1000-2000 independent seeded trials per coverage assertion to keep the ±2pp margin comfortably within statistical noise at 90-95% nominal coverage (binomial margin-of-error estimate, `[ASSUMED]` — see Assumptions Log A5)

## Security Domain

Not applicable in the ASVS sense — this phase touches no authentication, session, access-control, or cryptography surface. The one security-adjacent concern is **input validation** (ASVS V5), already covered by this phase's D-06 requirement (reject α ∉ (0,1) with a named error) and D-09 (reject unknown/removed strategy strings with a named error instead of silently falling back) — both are correctness requirements already tracked above, not a separate security control.

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V5 Input Validation | yes | α range check + named error (D-06); strategy/method string validation, no silent fallback (D-09) — both already Rust/C++ `InvalidInput`-coded errors per the established `ErrorCode` taxonomy (`src/include/anofox_fcst_ffi.h:72-82`) |
| V2/V3/V4/V6 | no | No auth/session/access-control/crypto surface in this phase |

## Sources

### Primary (HIGH confidence — code read directly this session)
- `crates/anofox-fcst-core/src/conformal.rs` (2280 lines, read in full-coverage sections: 1-180, 280-560, 650-1050, 1900-1990) — quantile clamp, asymmetric target, adaptive normalization, JackknifePlus, split_quantile panic site
- `crates/anofox-fcst-core/src/bootstrap.rs` (194 lines, read in full) — sign-convention bug
- `src/macros/ts_macros.cpp` lines 1700-1860 — all 4 conformal macro bodies
- `src/scalar_functions/conformal.cpp` lines 1-1000, 1118-1260 — scalar function wrappers, error-discard pattern, strategy/method parsing
- `src/include/anofox_fcst_ffi.h` lines 1440-1650, 3060-3360 — FFI struct/function signatures (confirms difficulty plumbing already exists in learn/apply v2 API)
- `crates/anofox-fcst-ffi/src/lib.rs` lines 5217-5370 — FFI boundary implementation pattern (catch_unwind, error propagation)
- `src/table_functions/ts_forecast_native.cpp` lines 733-734 — established error-code-gated throw pattern
- `~/.cargo/registry/src/.../anofox-forecast-0.15.10/src/postprocess/bootstrap.rs` lines 85-115 — upstream `fit()` sign-convention doc comment (ground truth for G2)
- `~/.cargo/registry/src/.../anofox-forecast-0.15.10/src/postprocess/conformal.rs` lines 470-480 — upstream `compute_quantile` panic site (ground truth for G16)
- `.planning/research/STAT-AUDIT-2026-10-08.md` §D (D4-D6), §G (G2, G12-G16) — original finding catalog, cross-checked against current line numbers
- `docs/api/11-conformal-prediction.md` — current documented "Coverage caveats" (already describes several of these bugs as known limitations, useful for the DOCS-01-style sweep)
- `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md` lines 149-296 — current skill text, including a parameter-order mismatch for `ts_bootstrap_intervals` vs. its actual C++ registration (skill doc shows `point_forecasts, residuals, ...`; code registers `residuals, forecasts, ...`)
- `test/sql/ts_conformal.test`, `ts_conformal_coverage.test`, `ts_conformal_null_policy.test` — confirms json-gating status of each (only `ts_conformal_null_policy.test` is already non-gated, and it tests scalar functions, not the 4 macros in scope)
- `CHANGELOG.md` lines 1-90 — established Breaking-changes entry format/voice to match
- `validation/reference/README.md`, `validation/reference/python/pyproject.toml`, `crates/anofox-fcst-core/tests/common/mod.rs` — fixture-contract and loader pattern to reuse
- `.planning/phases/16-conformal-bootstrap-correctness/16-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md` — upstream locked decisions and requirement text

### Secondary (MEDIUM confidence)
- None — no WebSearch/MCP lookups were needed; this is an entirely in-repo code-correctness phase with no new external packages or unfamiliar library APIs to research. All statistical-method citations below are standard, well-known references used only to confirm the *target* formula shape, not to resolve any implementation uncertainty.

### Tertiary (LOW confidence / training-knowledge citations, not verified via web lookup this session)
- Romano, Patterson & Candès (2019), "Conformalized Quantile Regression", NeurIPS — cited for the α/2 / 1-α/2 signed-residual-quantile construction underlying D-07/CONF-03. `[ASSUMED]` — not re-verified via WebSearch this session; the formula shape is also directly specified in CONTEXT.md's own D-07 wording, so this citation is supporting context, not load-bearing.
- Papadopoulos, Gammerman & Vovk (2008), "Normalized Nonconformity Measures for Regression Conformal Prediction"; Lei, G'Sell, Rinaldo, Tibshirani & Wasserman (2018), "Distribution-Free Predictive Inference for Regression", JASA — cited for the |r|/d normalized-score / d-scaled-prediction construction underlying D-08/CONF-04. `[ASSUMED]` — same caveat; CONTEXT.md's own D-08 wording ("Scores are |r|/d and intervals q·d") already fully specifies the target behavior independent of this citation.
- Barber, Candès, Ramdas & Tibshirani (2021), "Predictive inference with the jackknife+", Annals of Statistics — cited only to characterize what true jackknife+ requires (leave-one-out refits), supporting the D-09/deferred-idea rationale that it doesn't fit this architecture. `[ASSUMED]`.

## Metadata

**Confidence breakdown:**
- SQL macro audit (D-01..D-03 current state): HIGH — every claim is a direct `Read` of `ts_macros.cpp` at current HEAD this session, with exact line numbers
- Rust conformal/bootstrap bug locations (G2, G12-G16): HIGH — every claim is a direct `Read` of `conformal.rs`/`bootstrap.rs` this session, cross-checked against the audit's finding IDs and (for G2, G16) against the pinned upstream crate's actual source
- Correct target formulas (CQR asymmetric quantiles, normalized adaptive scores): MEDIUM — grounded in both CONTEXT.md's own precise wording (HIGH) and standard-reference citations not re-verified via web lookup this session (the citations are supporting, not load-bearing, per the Sources section)
- Monte Carlo sample-size recommendation: LOW-MEDIUM — standard statistical formula, not project-specific; flagged in Assumptions Log

**Research date:** 2026-10-12
**Valid until:** This research is tied to exact line numbers in `src/macros/ts_macros.cpp`, `crates/anofox-fcst-core/src/conformal.rs`/`bootstrap.rs`, and `src/scalar_functions/conformal.cpp` at the `fix/statistical-correctness` branch HEAD as of 2026-10-12. Phase 15 is executing concurrently on the same branch and may shift line numbers in unrelated files (it does not touch conformal/bootstrap per its own scope), but any Phase 15 commit before Phase 16 execution should prompt a quick line-number re-check rather than a full re-research. Valid for the remainder of this phase's planning/execution window (no fixed day-count expiry — this is an in-repo code audit, not an external-ecosystem fact that decays).
