# Phase 9: Reference Fixtures & Safe Dependency Bumps - Context

**Gathered:** 2026-10-08
**Status:** Ready for planning
**Mode:** Infrastructure phase (smart discuss skipped)

<domain>
## Phase Boundary

Build the reference-fixture harness every later phase of v0.10.0 uses to prove values (TEST-01), and land the low-risk dependency bumps: anofox-regression =0.5.3 → =0.5.15 (DEP-01), anofox-forecast 0.15.3 → 0.15.10 (DEP-02), and make the stated MSRV match the real dependency-tree requirement (DEP-04). No forecast-wrapper behaviour changes here (that is Phase 10); fdars-core stays on 0.3 (bumped in Phase 15).

</domain>

<decisions>
## Implementation Decisions

### Fixture harness
- Location `validation/reference/` with `r/` and `python/` generator scripts, output to `validation/reference/fixtures/<family>/*.json` (JSON: inputs, parameters, expected outputs, tool + package versions, tolerance).
- R is available with forecast, tseries, urca, jsonlite (feasts NOT installed — use stats::stl for STL strengths later). Python: create a uv project `validation/reference/python/pyproject.toml` pinning numpy, scipy, statsmodels, tsfresh, pandas (system python lacks statsmodels/tsfresh).
- Rust side: a small test-support loader (dev-dependency `serde_json`, already in tree via serde) usable from `crates/anofox-fcst-core/tests/` — fixtures loaded via `CARGO_MANIFEST_DIR`-relative path.
- Phase 9 seeds at least: AirPassengers dataset + R `forecast` reference (naive/snaive/rwf/ses/holt/ets/auto.arima/thetaf/croston points + 80/95 intervals), scipy moments (skew/kurtosis bias=False), numpy lstsq multi-scale OLS design, and ADF/KPSS/Ljung-Box R references — later phases add families.
- README documents regeneration commands, tool versions and per-family tolerances.

### Dependency bumps
- anofox-regression `=0.5.15` (compiles unchanged per audit); anofox-forecast `0.15.10` (compiles unchanged; behaviour diffs only MFLES #219, panel NaN guards, Laplace leaves).
- Capture a 0.15.3 point-forecast snapshot for all ts_forecast_by models BEFORE bumping, then diff after: only MFLES/panel/Laplace may change.
- Note: Phase 10 will change intervals/fitted values anyway; the snapshot compares point forecasts only.

### MSRV
- Determine the true MSRV (audit: `trueno` dependency needs > 1.86) with `cargo +<ver> check`/`cargo msrv`-style bisection; set `rust-version` in workspace Cargo.toml; update README/CLAUDE.md/docs/CI pins accordingly.

### Claude's Discretion
All other implementation choices are at Claude's discretion — infrastructure phase.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Audit reference scripts (may be ephemeral): `/tmp/claude-1000/-home-simonm-projects-duckdb-anofox-forecast/ee86fffc-9d6b-4781-a89d-aacb4f70fb57/scratchpad/{upstream_af/data/ref.R, upstream_fdars_reg/cmp_reg.py, core_stats, forecast_ffi}` — port useful parts into validation/reference/.
- Upstream crate has its own `~/projects/rust/anofox-forecast/validation/` (statsforecast comparisons) for patterns.
- `benchmark/` uses uv already (pyproject pattern).

### Established Patterns
- Rust tests inline `#[cfg(test)]`; `approx` dev-dependency in core.
- DoD: cargo test + cargo fmt --check + clippy (CI "Rust Tests" job gates on rustfmt).
- SQL examples must be run against the built extension (no build dir currently on branch — build with the Makefile `release` target).

### Integration Points
- Workspace `Cargo.toml` (dependency pins), `Cargo.lock`, `crates/anofox-fcst-core/Cargo.toml`.
- `ts_forecast_exog_by` macro + `forecast.rs` `fit_ols_regression` for the DEP-01 SQL check.

</code_context>

<specifics>
## Specific Ideas

- Audit evidence to reproduce as fixtures: multi-scale OLS design where 0.5.3 scrambled coefficients (price ~10, promo 0/1, temperature sd 8, holiday 0/1) → coefficient ≈ −8.8 vs −79; 15k-row exog fit memory (1.7 GB on 0.5.3 vs 11 MB on 0.5.15).
- Executor reports must be verified against git + disk (known fabrication risk); worktrees disabled.

</specifics>

<deferred>
## Deferred Ideas

None — infrastructure phase.

</deferred>
