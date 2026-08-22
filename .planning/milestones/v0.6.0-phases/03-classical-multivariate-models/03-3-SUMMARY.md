---
phase: 03-classical-multivariate-models
plan: "03"
subsystem: benchmarks-docs
tags: [garch, kalman, var, benchmark, arch, statsmodels, documentation, skill]
status: complete

requires:
  - phase: 03-1
    provides: GARCH/Kalman via ts_forecast_by; ForecastOptions ABI extended; classical_forecasting_examples.sql
  - phase: 03-2
    provides: VAR via ts_forecast_var_by; VARForecastResult FFI; _ts_forecast_var_native

provides:
  - GARCH benchmark vs arch GARCH(1,1) on M4 Daily returns — parity ratio 0.897 (PASS)
  - Kalman benchmark vs statsmodels UnobservedComponents — local_level 1.000, llt 0.992 (both PASS)
  - VAR benchmark vs statsmodels.tsa.api.VAR on synthetic VAR(1) — MAE ratio 1.000 (exact match, PASS)
  - arch>=5.3.0 added to benchmark/pyproject.toml [comparison] group; installed as arch 8.0.0
  - docs/reference/models/classical/garch.md (new dir; volatility-not-variance explicitly documented)
  - docs/reference/models/state-space/kalman.md (local_level + local_linear_trend)
  - docs/reference/models/multivariate/var.md (new dir; ts_forecast_var_by; p param; long format)
  - docs/api/07-forecasting.md Classical Models + Multivariate sections; model count 33→36
  - .claude/skills/anofox-forecast-models/SKILL.md updated with GARCH/Kalman/VAR; model count 33→36

affects:
  - Any phase generating GARCH/Kalman/VAR forecasts (doc and SKILL are the reference)
  - Phase 4+ (CLAS-01/02/03 now fully complete per milestone DoD)

actuals:
  tokens: 28000
  tasks: 4
  commits: 3

tech-stack:
  added:
    - arch>=5.3.0 (arch 8.0.0 installed) — benchmark/pyproject.toml [comparison] group
  patterns:
    - "GARCH benchmark: M4 Daily returns (first differences) → anofox CLI subprocess → arch GARCH(1,1); behavioral/approximate parity (ratio=0.897)"
    - "Kalman benchmark: M4 Daily levels → anofox CLI subprocess → statsmodels UnobservedComponents; fixed-vs-MLE variance explains non-exact parity"
    - "VAR benchmark: synthetic VAR(1) (c=[0.5,0.3], A=[[0.6,0.1],[0.05,0.7]], N=200, seed=42) → anofox + statsmodels; both use OLS → ratio=1.000"
    - "All benchmark run.py scripts use CLI subprocess pattern (build/release/duckdb -unsigned) to avoid venv/extension ABI mismatch (Phase 1 precedent)"
    - "All doc SQL snippets verified end-to-end against built extension before commit (PR #230 rule)"

key-files:
  created:
    - benchmark/configs/garch.py
    - benchmark/configs/kalman.py
    - benchmark/configs/var.py
    - benchmark/m4/garch_benchmark/run.py
    - benchmark/m4/garch_benchmark/results/ (5 parquet files)
    - benchmark/m4/kalman_benchmark/run.py
    - benchmark/m4/kalman_benchmark/results/ (8 parquet files)
    - benchmark/m4/var_benchmark/run.py
    - benchmark/m4/var_benchmark/results/ (5 parquet files)
    - docs/reference/models/classical/garch.md
    - docs/reference/models/multivariate/var.md
  modified:
    - benchmark/pyproject.toml (arch>=5.3.0 added)
    - benchmark/uv.lock (arch 8.0.0 + transitive deps)
    - docs/reference/models/state-space/kalman.md (new file — state-space dir existed)
    - docs/api/07-forecasting.md (Classical + Multivariate sections; model count 33→36)
    - .claude/skills/anofox-forecast-models/SKILL.md (GARCH/Kalman/VAR; model count 33→36)

key-decisions:
  - "arch path chosen (not variance-convergence fallback): arch 8.0.0 resolves from PyPI; uv dry-run confirmed; installed successfully in venv"
  - "GARCH benchmark uses M4 Daily first-differences as returns; raw levels rejected per Pitfall 9 (near-IGARCH divergence)"
  - "Kalman benchmark caps at 50 series (statsmodels MLE per series is slow ~0.35s each); sufficient for behavioral parity check"
  - "VAR benchmark uses synthetic data (no multivariate M4 exists); both anofox and statsmodels use OLS → ratio=1.000 (exact algorithmic match)"
  - "All doc SQL snippets copied from classical_forecasting_examples.sql (already verified in 03-1/03-2) and re-verified against the built extension (PR #230)"

patterns-established:
  - "Classical model benchmarks (no M4 analog): standalone run.py scripts with synthetic or preprocessed data; not using create_benchmark_functions harness"
  - "Benchmark reference path: arch for GARCH, statsmodels UnobservedComponents for Kalman, statsmodels VAR for VAR"
  - "Model doc format: Signature → volatility-not-variance warning (GARCH only) → Description → Parameters → Returns → SQL Examples (verified) → Model Details → Common Pitfalls → Benchmark → Reference"

requirements-completed: [CLAS-01, CLAS-02, CLAS-03]

coverage:
  - id: D1
    description: "GARCH benchmark: arch vs anofox on M4 Daily returns; parity ratio 0.897 (PASS); results committed"
    requirement: CLAS-01
    verification:
      - kind: e2e
        ref: "benchmark/.venv/bin/python benchmark/m4/garch_benchmark/run.py run → garch-evaluation-Daily.parquet parity=PASS ratio=0.897"
        status: pass
    human_judgment: false
  - id: D2
    description: "Kalman benchmark: statsmodels UnobservedComponents vs anofox on M4 Daily; local_level ratio=1.000, llt ratio=0.992 (both PASS)"
    requirement: CLAS-02
    verification:
      - kind: e2e
        ref: "benchmark/.venv/bin/python benchmark/m4/kalman_benchmark/run.py run → kalman-evaluation-Daily.parquet both parity=PASS"
        status: pass
    human_judgment: false
  - id: D3
    description: "VAR benchmark: statsmodels VAR vs anofox ts_forecast_var_by on synthetic VAR(1); MAE ratio=1.000 (PASS)"
    requirement: CLAS-03
    verification:
      - kind: e2e
        ref: "benchmark/.venv/bin/python benchmark/m4/var_benchmark/run.py run → var-evaluation-p1.parquet y1+y2 both PASS ratio=1.000"
        status: pass
    human_judgment: false
  - id: D4
    description: "docs/reference/models/classical/garch.md: GARCH model page with volatility-not-variance warning, params, pitfalls, benchmark results"
    requirement: CLAS-01
    verification:
      - kind: e2e
        ref: "test -f docs/reference/models/classical/garch.md && grep -qi 'volatility' docs/reference/models/classical/garch.md"
        status: pass
      - kind: e2e
        ref: "build/release/duckdb -unsigned < /tmp/test_garch_doc.sql → 7 rows, model_name=GARCH(1,1)"
        status: pass
    human_judgment: false
  - id: D5
    description: "docs/reference/models/state-space/kalman.md: Kalman model page with local_level + local_linear_trend specs, kalman_model param"
    requirement: CLAS-02
    verification:
      - kind: e2e
        ref: "test -f docs/reference/models/state-space/kalman.md"
        status: pass
      - kind: e2e
        ref: "build/release/duckdb -unsigned < /tmp/test_kalman_doc.sql → 7 rows each spec"
        status: pass
    human_judgment: false
  - id: D6
    description: "docs/reference/models/multivariate/var.md: VAR model page with ts_forecast_var_by signature, p param, long-format output, pitfalls"
    requirement: CLAS-03
    verification:
      - kind: e2e
        ref: "test -f docs/reference/models/multivariate/var.md"
        status: pass
      - kind: e2e
        ref: "build/release/duckdb -unsigned < /tmp/test_var_doc.sql → 28 rows, 2 variables"
        status: pass
    human_judgment: false
  - id: D7
    description: "docs/api/07-forecasting.md: Classical Models (GARCH+Kalman) and Multivariate (VAR) sections added; model count 33→36"
    verification:
      - kind: e2e
        ref: "grep -c 'Classical Models' docs/api/07-forecasting.md → 1"
        status: pass
    human_judgment: false
  - id: D8
    description: ".claude/skills/anofox-forecast-models/SKILL.md updated: GARCH entry (volatility-not-variance), Kalman entry, ts_forecast_var_by section, model count 33→36"
    verification:
      - kind: e2e
        ref: "grep -qi 'GARCH' .claude/skills/anofox-forecast-models/SKILL.md && grep -qi 'Kalman' ... && grep -qi 'ts_forecast_var_by' ..."
        status: pass
    human_judgment: false

duration: 9 min
completed: "2026-08-21"
---

# Phase 03 Plan 03: Benchmarks + Docs + SKILL — CLAS-01/02/03 DoD Complete

**Reference cross-check benchmarks (GARCH/arch ratio=0.897, Kalman/statsmodels ratio=1.000/0.992, VAR/statsmodels ratio=1.000) + three model doc pages + 07-forecasting.md Classical/Multivariate sections + SKILL.md update, completing CLAS-01/02/03 milestone DoD.**

## Performance

- **Duration:** 9 min
- **Started:** 2026-08-21T22:30:30Z
- **Completed:** 2026-08-21T22:39:52Z
- **Tasks completed:** 4 (Tasks 0-3; Task 0 is the arch legitimacy gate)
- **Files changed:** 30+ (benchmark configs, run.py scripts, results parquet, docs, SKILL.md)
- **Commits:** 3

## Accomplishments

1. **GARCH benchmark — arch 8.0.0 path (Task 0 + Task 1, commit 257f945):** Confirmed arch resolves from PyPI (version 8.0.0, Kevin Sheppard), added `arch>=5.3.0` to `benchmark/pyproject.toml` [comparison] group, installed via `uv sync --extra comparison`. Created `benchmark/configs/garch.py` + `benchmark/m4/garch_benchmark/run.py`: converts M4 Daily series to returns (first differences), runs anofox GARCH(1,1) via CLI subprocess, compares against `arch.arch_model('Zero','Garch',p=1,q=1)`. Parity ratio = **0.897** (PASS; target 0.1–10.0). 100 series, 1,400 forecast rows each side.

2. **Kalman benchmark (Task 1, commit 257f945):** `benchmark/m4/kalman_benchmark/run.py` runs anofox Kalman (local_level + local_linear_trend) via CLI subprocess, compares against `statsmodels.tsa.statespace.structural.UnobservedComponents`. 50 series. local_level ratio = **1.000**, local_linear_trend ratio = **0.992** (both PASS; target 0.5–2.0). Fixed-vs-MLE variance explains non-exact match.

3. **VAR benchmark — synthetic data (Task 1, commit 257f945):** `benchmark/m4/var_benchmark/run.py` generates synthetic VAR(1) data (c=[0.5,0.3], A=[[0.6,0.1],[0.05,0.7]], N=200, seed=42), runs anofox `ts_forecast_var_by` via CLI subprocess, compares against `statsmodels.tsa.api.VAR`. Both use OLS equation-by-equation → MAE ratio = **1.000** (exact algorithmic match, PASS). 28 long-format rows each.

4. **Docs (Task 2, commit 1713a46):** Three new model pages: `docs/reference/models/classical/garch.md` (explicitly documents forecast_value=volatility/std-dev NOT variance), `docs/reference/models/state-space/kalman.md` (local_level + local_linear_trend), `docs/reference/models/multivariate/var.md` (ts_forecast_var_by; p param; long format; pitfalls). Extended `docs/api/07-forecasting.md` with Classical Models section (GARCH+Kalman) and Multivariate section (VAR) after the Panel section; model count updated 33→36. All SQL snippets verified end-to-end against the built extension (PR #230 rule).

5. **SKILL.md (Task 3, commit dab0866):** Updated `.claude/skills/anofox-forecast-models/SKILL.md`: GARCH entry in new "Classical volatility" section (volatility-not-variance warning, min-obs note, returns-only pitfall, benchmark ratio=0.897); Kalman added to state-space section (kalman_model param, two specs, benchmark ratios); new `ts_forecast_var_by` section (full signature, value_cols VARCHAR[], p named param, long-format output, pitfalls table, benchmark ratio=1.000); model count 33→36 in description and catalogue header.

## Task Commits

1. **Task 0 + Task 1: Benchmarks (GARCH/Kalman/VAR) with committed results** - `257f945` (feat)
2. **Task 2: Docs (garch.md, kalman.md, var.md, 07-forecasting.md)** - `1713a46` (feat)
3. **Task 3: SKILL.md update** - `dab0866` (feat)

## Deviations from Plan

None — plan executed exactly as written. Task 0 (arch legitimacy gate) confirmed the primary arch path (not fallback); all three benchmarks ran and produced committed results; docs verified end-to-end; SKILL.md updated.

## Issues Encountered

None. Notable observations:
- VAR benchmark produced ratio=1.000 (exact match) because both anofox and statsmodels use OLS equation-by-equation — algorithmically identical on the same data.
- Kalman ratio=1.000 for local_level (near-exact despite different variance estimation): statsmodels local level on M4 Daily series rapidly converges the Kalman gain, so the MLE-estimated variances produce nearly the same filtered state as fixed variances over long series.
- statsmodels coefficient recovery for synthetic VAR(1): max relative error=1.34 (high) because the small noise (Uniform(-0.01,0.01)) makes the signal nearly deterministic, amplifying relative error on the small off-diagonal A[0][1]=0.1 and A[1][0]=0.05 terms. The actual forecast MAE is still excellent (ratio=1.000).

## Self-Check: PASSED

- `docs/reference/models/classical/garch.md` — FOUND
- `docs/reference/models/state-space/kalman.md` — FOUND
- `docs/reference/models/multivariate/var.md` — FOUND
- `benchmark/m4/garch_benchmark/results/garch-evaluation-Daily.parquet` — FOUND
- `benchmark/m4/kalman_benchmark/results/kalman-evaluation-Daily.parquet` — FOUND
- `benchmark/m4/var_benchmark/results/var-evaluation-p1.parquet` — FOUND
- `grep -qi 'volatility' docs/reference/models/classical/garch.md` — PASS
- `grep -qi 'ts_forecast_var_by' .claude/skills/anofox-forecast-models/SKILL.md` — PASS
- Commits 257f945, 1713a46, dab0866 — confirmed in git log
- All SQL snippets verified end-to-end against `build/release/duckdb -unsigned` — PASS
