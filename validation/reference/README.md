# Reference-Fixture Harness (TEST-01)

## 1. Purpose

Every statistic and model-point value the `anofox_forecast` extension computes must
be verified against an independent reference implementation — R's `forecast` /
`tseries` / `urca` ecosystem, or Python's `scipy` / `numpy` / `statsmodels` /
`tsfresh` — before it is trusted. This directory is the harness that makes that
verification repeatable:

- `r/` and `python/` hold checked-in **generator scripts**. Running a generator
  produces one or more JSON (plus, for some families, CSV) **fixture files** under
  `fixtures/<family>/`.
- Fixtures are **ground truth**: they carry the exact input data, the exact R/Python
  call that produced the expected output, the tool/package versions that produced
  it, and the tolerance a consuming test should use. They are regenerated only
  deliberately — never as a side effect of running `cargo test`.
- Rust (and, where noted, SQL) tests load these fixtures and assert the product's
  output against them within the fixture's own declared tolerance.

This harness is seeded in Phase 9 and extended by every later phase of the
`v0.10.0` "Statistical Correctness" milestone — each new statistical family gets
its own fixture family here, following the contract in section 3.

## 2. Layout

```
validation/reference/
├── README.md                      # this file
├── r/
│   ├── common.R                   # script-dir resolution, provenance, shared reference_series()
│   ├── gen_forecast_reference.R   # writes r_forecast/{air_passengers,simulated_series}.json
│   ├── gen_diagnostics_reference.R# writes r_diagnostics/stationarity_portmanteau.json
│   └── ...                        # later phases add generators here
├── python/
│   ├── pyproject.toml             # uv project: numpy, scipy, pandas, statsmodels, tsfresh (package = false)
│   ├── uv.lock                    # committed lockfile — `uv run --locked` refuses a drifted lock
│   ├── fixture_io.py              # shared provenance + strict-JSON/CSV writers
│   ├── gen_moments.py             # writes moments/scipy_moments.json
│   ├── gen_multiscale_ols.py      # writes multiscale_ols/{cases.json,exog_typical.csv}
│   ├── gen_panel_nan_guard.py     # writes panel_nan_guard/cases.json
│   └── ...                        # later phases add generators here
└── fixtures/
    ├── r_forecast/{air_passengers,simulated_series}.json
    ├── r_diagnostics/stationarity_portmanteau.json
    ├── moments/scipy_moments.json
    ├── multiscale_ols/{cases.json,exog_typical.csv}
    ├── panel_nan_guard/cases.json
    └── <family>/...                # later phases add families here
```

The Rust loader lives outside this directory, at
`crates/anofox-fcst-core/tests/common/mod.rs` — see section 7.

## 3. Fixture contract

Every fixture JSON file MUST be a JSON object with these top-level keys (enforced
automatically by `fixture_provenance_contract` in
`crates/anofox-fcst-core/tests/reference_fixtures.rs`, for every fixture present
today and every fixture a later phase adds):

| Key | Meaning |
|-----|---------|
| `family` | Must equal the fixture's parent directory name (e.g. `moments` for `fixtures/moments/scipy_moments.json`) |
| `generator` | Repo-relative POSIX path of the script that produced this file (e.g. `validation/reference/python/gen_moments.py`); must resolve to an existing file |
| `tool_versions` | Non-empty object of tool/package name → exact version string that produced this file |
| `tolerance` | Non-empty object of tolerance keys a consuming test reads via `common::tolerance(&fx, key)` — never hard-code a tolerance in a Rust test |
| (family-specific) | `inputs`/`series`/`vectors`/`cases`/`parameters` — the actual input data, parameters, and expected outputs; shape is per-family, not a rigid schema (see Open Question 1 in 09-RESEARCH.md — the shape is left to stabilize organically) |

**Numeric encoding:** a fixture value is either a JSON number, or (for non-finite
floats) one of the strings `"NaN"`/`"NA"` (not-a-number), `"Inf"`/`"Infinity"`
(positive infinity), `"-Inf"`/`"-Infinity"` (negative infinity) — decoded by
`common::num()`. R's `jsonlite` and this repo's `fixture_io.py` both emit this
encoding; `json.dumps(..., allow_nan=False)` on the Python side guarantees the
file itself is strict JSON (no bare `NaN`/`Infinity` literals).

**CSV companions:** a `*.csv` file under a fixture family directory MUST have a
sibling `*.json` file in the same directory carrying that family's provenance —
`fixture_provenance_contract` enforces this too. The CSV itself carries only the
tabular data; tolerance and tool versions live in the JSON sibling.

**Tests must read tolerances from the fixture, never hard-code them** — this is
what makes a tolerance change in a regenerated fixture visible to every consuming
test without a separate Rust edit.

## 4. Families

| Family | Files | Generator | Tolerance keys (values) | Consumers |
|--------|-------|-----------|--------------------------|-----------|
| `r_forecast` | `air_passengers.json`, `simulated_series.json` | `r/gen_forecast_reference.R` | `closed_form_abs` (1e-8), `model_point_rel` (0.01), `interval_rel` (0.05) | Phase 9 (closed-form Naive/SeasonalNaive/RandomWalkDrift), Phase 10 (FCST-01 model-point parity), Phase 17 (FCST-09 full R parity) |
| `r_diagnostics` | `stationarity_portmanteau.json` | `r/gen_diagnostics_reference.R` | `statistic_abs` (1e-6), `p_value_abs` (1e-4) | Phase 11 (upstream diagnostics), Phase 17 (DIAG-03..05) |
| `moments` | `scipy_moments.json` | `python/gen_moments.py` | `abs` (1e-12), `rel` (1e-12) | Phase 9 (self-consistency pin), Phase 13 (STAT-01 `ts_stats` moments) |
| `multiscale_ols` | `cases.json`, `exog_typical.csv` | `python/gen_multiscale_ols.py` | `coef_rel` (1e-8), `coef_abs` (1e-9), `fitted_abs_per_sd` (1e-8), `sql_point_abs` (1.0) | Phase 9: `forecast.rs::tests::fit_ols_regression_matches_numpy_lstsq_fixture` (DEP-01, inline), `tests/exog_ols_memory.rs` (15k-row peak-heap guard), `validation/reference/sql/check_dep01_exog.sql` (SQL-level, DEP-01) |
| `panel_nan_guard` | `cases.json` | `python/gen_panel_nan_guard.py` | outcome-class only (`abs: 0.0`, see fixture `tolerance.notes`) | Phase 9: `anofox-fcst-ffi::panel_ffi_tests::panel_nan_guard_fixture_outcomes` (DEP-02) |
| `forecast_snapshot` | `point_forecasts.json` | `crates/anofox-fcst-core/tests/forecast_snapshot.rs` (bless mode: `ANOFOX_BLESS_FORECAST_SNAPSHOT=1 cargo test -p anofox-fcst-core --test forecast_snapshot`) | `point_abs` (1e-6), `point_rel` (1e-6) | Phase 9 (DEP-02 pre/post-bump point-forecast parity for every `ts_forecast_by` model), and any later phase that deliberately changes point forecasts — re-blessing replaces the anofox-forecast 0.15.3 baseline, so record the reason in that phase's SUMMARY. `allowed_to_change`: MFLES, AutoMFLES, Laplace. |
| `r_entropy` | `approx_entropy.json` | `r/gen_entropy_reference.R` | `abs` (1e-12) | Phase 13 (STAT-03, D-03): `ts_stats` `entropy` column (Pincus approximate entropy, m=2, r=0.2*sd) |
| `gap_counts` | `cases.json` | `python/gen_gap_counts.py` | `count_abs` (0, exact integer counts) | Phase 13 (STAT-03, D-04): `ts_stats` `n_gaps`/`n_gap_runs`/`expected_length` (13-01); Phase 13 data quality gap/temporal scoring and month-end-aware gap fill (`fill_cases`, 13-03) |
| `quality_scores` | `cases.json` | `python/gen_quality_reference.py` | `score_abs` (1e-12) | Phase 13 (D-05/D-06, STAT-04, 13-03): `anofox_fcst_core::quality::compute_data_quality` -- all five scores, `n_gaps`/`n_gap_runs`/`n_missing`/`is_constant`/`is_short` |
| `r_mase` | `accuracy_mase.json` | `r/gen_mase_reference.R` | `mase_abs` (1e-12) | Phase 13 (D-08, STAT-06, 13-04): `anofox_fcst_core::metrics::mase` (Hyndman-Koehler MASE, scaled by the in-sample seasonal-naive MAE at lag `seasonal_period`) -- each case carries BOTH `mase_accuracy` (`forecast::accuracy()`) and `mase_manual` (the formula), which must agree |

## 5. Tool versions

These are the toolchain versions this phase verified against. **Each fixture's own
`tool_versions` block is authoritative** — if a later regeneration bumps a package,
the fixture records the new version and this table becomes historical context, not
a live source of truth.

- **R** 4.6.1, with `forecast` 9.0.2, `tseries` 0.10.63, `urca` 1.3.4, `jsonlite`
  2.0.0. `feasts` is **not installed** — any STL-strength family (STAT-02) must use
  `stats::stl` instead.
- **Python** 3.12, managed via `uv` (see `python/pyproject.toml` / `uv.lock`), with
  `numpy` 2.5.3, `scipy` 1.18.1, `pandas` 3.0.6, `statsmodels` 0.15.0, `tsfresh`
  0.21.2.

## 6. Regeneration

Regenerate a family only deliberately — never as a side effect of running tests.

```bash
# R families (run from anywhere; common.R resolves paths relative to the script):
Rscript validation/reference/r/gen_forecast_reference.R
Rscript validation/reference/r/gen_diagnostics_reference.R

# Python families (uv project is cwd-independent via --project):
uv run --locked --project validation/reference/python python validation/reference/python/gen_moments.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_multiscale_ols.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_panel_nan_guard.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_gap_counts.py
uv run --locked --project validation/reference/python python validation/reference/python/gen_quality_reference.py

# R families added in Phase 13:
Rscript validation/reference/r/gen_entropy_reference.R
Rscript validation/reference/r/gen_mase_reference.R

# Determinism check — every generator is seeded and writes no timestamps/hostnames,
# so regenerating with the same toolchain versions must produce an empty diff:
git diff --exit-code -- validation/reference/fixtures
```

If `git diff` is non-empty after a deliberate regeneration, either a package version
changed (expected — update `tool_versions` by committing the diff) or a generator
has a latent non-determinism bug (investigate before committing).

## 7. Consuming fixtures from Rust

**From an integration test** (`crates/anofox-fcst-core/tests/*.rs`, the normal case
— sees only the crate's public API):

```rust
mod common;
use common::{load_fixture, f64_vec, num, tolerance, assert_close, assert_vec_close};

let fx = load_fixture("multiscale_ols/cases.json");
let tol = tolerance(&fx, "coef_abs");
```

**From an inline `#[cfg(test)]` block inside `src/`** (needed when the function
under test is private, e.g. `fit_ols_regression` in `forecast.rs` — integration
tests cannot see non-`pub` items): the library includes the same loader via a
`#[path = "../tests/common/mod.rs"]` module (added in 09-03), which is why
`crates/anofox-fcst-core/tests/common/mod.rs` must stay free of any
`anofox_fcst_core`/`anofox_fcst_ffi` crate references — it has to compile
standalone as an integration test AND be re-pathed into the library's own
`cfg(test)` build.

`anofox-fcst-ffi` also includes the same loader this way (09-03): a
`#[cfg(test)] #[path = "../../anofox-fcst-core/tests/common/mod.rs"]` module
right above `panel_ffi_tests` in `crates/anofox-fcst-ffi/src/lib.rs`, reaching
the identical file `anofox-fcst-core` itself `#[path]`s in — not a duplicate
copy. `repo_root()` resolves correctly from either crate because both sit two
directories below the workspace root.

## 9. SQL checks

Some fixtures are additionally proven at the SQL level, against the actual
**built extension** (not just the Rust crates), because the FFI/SQL layer adds
behaviour (date handling, column dispatch, the `ts_forecast_exog_by` macro
expansion) that a Rust-only test cannot see.

| Script | Fixture | Run command | Expected output |
|--------|---------|--------------|------------------|
| `sql/check_dep01_exog.sql` | `multiscale_ols/{cases.json,exog_typical.csv}` | `python3 -c "import resource,subprocess,sys;rc=subprocess.run(sys.argv[1:],stdin=open('validation/reference/sql/check_dep01_exog.sql')).returncode;print('MAXRSS_KB',resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss);sys.exit(rc)" ./build/release/duckdb -unsigned -bail` (run from the repo root, after `make release` / `GEN=ninja make release`) | `DEP01_SQL_PASS typical` and `DEP01_SQL_PASS 15k`, exit 0 |

Needs the extension built (`make release`) and the `json` extension available
(autoloaded by DuckDB on first use if not statically linked). Tolerance is
read from `multiscale_ols/cases.json`'s `tolerance.sql_point_abs` at run time,
never hardcoded in the script.

## 8. Adding a family (Phases 10-18)

1. Write a generator in `r/` or `python/` (whichever tool owns the reference
   implementation for the statistic in question).
2. Use the shared provenance helpers — `provenance()`/`write_fixture()` in
   `r/common.R`, or `generator_path()`/`tool_versions()`/`write_fixture()` in
   `python/fixture_io.py` — so `fixture_provenance_contract` accepts the new file
   for free.
3. Choose tolerance keys with a one-line rationale (why this bound, not a tighter
   or looser one) and record them in the fixture's own `tolerance` object.
4. Write a Rust test (integration test under `tests/`, or inline `#[cfg(test)]` if
   testing a private function) that loads the fixture and asserts the product's
   output against it, reading tolerance from the fixture.
5. Add a row to the Families table in section 4 of this README.
