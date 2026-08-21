# Diagnostic Benchmark / Cross-Check Harness

This directory contains the numeric cross-check harness for the `anofox-forecast`
statistical diagnostic functions (Phase 1: STAT-01 ADF).

## Purpose

Every diagnostic function must be numerically cross-checked against the reference
implementation (`statsmodels` for ADF/KPSS/LB/DW/JB; `R` is acceptable as secondary).
This harness establishes that tolerance.

## Files

| File | Description |
|------|-------------|
| `reference_values.py` | Generates `reference_adf.json` via `statsmodels.tsa.stattools.adfuller` |
| `run_anofox.py` | Loads the built DuckDB extension, runs `ts_adf`, compares against reference |
| `reference_adf.json` | Generated reference values (git-ignored; regenerate if fixture changes) |

**Reserved for plans 01-2 / 01-3:**

| File | Description |
|------|-------------|
| `reference_kpss.json` | KPSS reference values (statsmodels `kpss`) |
| `reference_residuals.json` | LjungBox / DW / JB reference values |
| `run_anofox_residuals.py` | Residual diagnostic cross-check (RESID-01..04) |

## Statsmodels Mapping

| anofox function | statsmodels function | Notes |
|-----------------|---------------------|-------|
| `ts_adf` | `statsmodels.tsa.stattools.adfuller(s, regression='c', autolag='AIC')` | Constant-only regression |
| `ts_kpss` (01-2) | `statsmodels.tsa.stattools.kpss(s, regression='c', nlags='auto')` | |
| `ts_ljung_box` (01-3) | `statsmodels.stats.diagnostic.acorr_ljungbox(s, lags=[lags])` | |
| `ts_durbin_watson` (01-3) | `statsmodels.stats.stattools.durbin_watson(s)` | No p-value |
| `ts_jarque_bera` (01-3) | `statsmodels.stats.stattools.jarque_bera(s)` | |

## Numeric Tolerances

| Metric | Tolerance | Rationale |
|--------|-----------|-----------|
| Test statistic | `rtol=0.01` (1%) | OLS regression value; differs only due to floating-point implementation details |
| p-value | `rtol=0.10` (10%) | Approximate from 9-point MacKinnon lookup table (piecewise-linear interpolation). Rounding to nearest breakpoint can shift values by ~10% near the table endpoints. |

## Running the Cross-Check

```bash
# Step 1: Generate reference values (uses benchmark venv, not system python3)
benchmark/.venv/bin/python benchmark/diagnostics/reference_values.py

# Step 2: Run cross-check against the built extension
benchmark/.venv/bin/python benchmark/diagnostics/run_anofox.py

# Or from the benchmark directory using uv:
cd benchmark && uv run python diagnostics/reference_values.py
cd benchmark && uv run python diagnostics/run_anofox.py
```

The extension path defaults to:
`./build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`

Override with `ANOFOX_EXTENSION_PATH=/path/to/extension`:

```bash
ANOFOX_EXTENSION_PATH=/path/to/anofox_forecast.duckdb_extension \
  benchmark/.venv/bin/python benchmark/diagnostics/run_anofox.py
```

## Why Not System python3?

`statsmodels 0.14.5` and `scipy 1.15.3` are only available inside
`benchmark/.venv` (managed by `uv`). System `python3` lacks them.
Always use `benchmark/.venv/bin/python` or `cd benchmark && uv run python`.
