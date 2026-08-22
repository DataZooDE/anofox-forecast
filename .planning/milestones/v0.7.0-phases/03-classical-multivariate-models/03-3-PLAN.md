---
phase: 03-classical-multivariate-models
plan: 03
type: execute
wave: 3
depends_on: [03-01, 03-02]
files_modified:
  - benchmark/pyproject.toml
  - benchmark/configs/garch.py
  - benchmark/configs/kalman.py
  - benchmark/configs/var.py
  - benchmark/m4/garch_benchmark/run.py
  - benchmark/m4/kalman_benchmark/run.py
  - benchmark/m4/var_benchmark/run.py
  - docs/api/07-forecasting.md
  - docs/reference/models/classical/garch.md
  - docs/reference/models/state-space/kalman.md
  - docs/reference/models/multivariate/var.md
  - .claude/skills/anofox-forecast-models/SKILL.md
autonomous: true
requirements: [CLAS-01, CLAS-02, CLAS-03]
estimate:
  tokens: 74000
  raw_tokens: 49000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "GARCH is benchmarked against arch (or the documented variance-convergence fallback) with committed results under benchmark/m4/garch_benchmark/results/ (success criterion 4)"
    - "Kalman is benchmarked against statsmodels UnobservedComponents with committed results"
    - "VAR is benchmarked on a synthetic VAR(1) dataset against statsmodels.tsa.api.VAR with committed results (success criterion 3 parity)"
    - "GARCH/Kalman/VAR each documented in docs/reference/models/ and docs/api/07-forecasting.md, every SQL example verified end-to-end (PR #230)"
    - "GARCH docs explicitly state forecast_value is volatility (std-dev), not variance"
    - "anofox-forecast-models SKILL.md updated with the GARCH/Kalman/VAR surface"
  artifacts:
    - benchmark/m4/garch_benchmark/run.py
    - benchmark/m4/kalman_benchmark/run.py
    - benchmark/m4/var_benchmark/run.py
    - docs/reference/models/classical/garch.md
    - docs/reference/models/state-space/kalman.md
    - docs/reference/models/multivariate/var.md
    - docs/api/07-forecasting.md
  key_links:
    - "benchmark run.py → benchmark/.venv/bin/python → build/release/duckdb CLI subprocess → committed results/*.parquet"
    - "docs SQL examples → verified against built extension (PR #230)"
  prohibitions:
    - "MUST NOT run benchmarks/cross-checks with system python3 — use benchmark/.venv/bin/python (Phase-1 precedent; statsmodels/arch live only in the venv)"
    - "MUST NOT commit docs with unverified SQL examples — every snippet must run against the built extension (PR #230 rule)"
    - "MUST NOT add arch to pyproject.toml without confirming legitimacy first (arch is verified legitimate — Kevin Sheppard, Production/Stable — proceed; if install is blocked, use the variance-convergence fallback)"
    - "MUST NOT claim exact numeric parity — behavioral/approximate parity only (same standard as Phases 1-2)"
---

<objective>
Close the Definition-of-Done for all three models: committed reference cross-check benchmarks under benchmark/ and full docs (docs/api/ + docs/reference/models/) with end-to-end-verified SQL examples. This plan makes CLAS-01/02/03 "Complete" per the milestone DoD (example already delivered in 03-1/03-2; this adds docs + reference cross-check).

Handles the known benchmark gap: `arch` (the standard Python GARCH reference) is not in benchmark/.venv. It is verified-legitimate (Kevin Sheppard, Production/Stable since 2014), so the primary path adds `arch` to benchmark/pyproject.toml; a documented variance-convergence self-check is the fallback if install is blocked.

Purpose: Satisfy roadmap success criteria 3 (VAR benchmark parity) and 4 (all three documented + cross-checked).
Output: Three benchmark configs + run scripts with committed results, three model doc pages + the 07-forecasting.md Classical/Multivariate sections, and an updated SKILL.md.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/03-classical-multivariate-models/03-RESEARCH.md
@.planning/phases/03-classical-multivariate-models/03-PATTERNS.md
@.planning/phases/03-classical-multivariate-models/03-CONTEXT.md
</context>

<artifacts_produced>
## Artifacts this plan produces (NEW files)

- benchmark/configs/garch.py, kalman.py, var.py (benchmark model configs)
- benchmark/m4/garch_benchmark/run.py + results/ (committed parquet)
- benchmark/m4/kalman_benchmark/run.py + results/ (committed parquet)
- benchmark/m4/var_benchmark/run.py + results/ (committed parquet — synthetic VAR(1))
- arch>=5.3.0 added to benchmark/pyproject.toml comparison group (if install path chosen)
- docs/reference/models/classical/garch.md (new dir)
- docs/reference/models/multivariate/var.md (new dir)
- docs/reference/models/state-space/kalman.md (existing dir)
- docs/api/07-forecasting.md Classical + Multivariate subsections
- .claude/skills/anofox-forecast-models/SKILL.md GARCH/Kalman/VAR entries
</artifacts_produced>

<tasks>

<task type="auto">
  <name>Task 0: Confirm arch package legitimacy, then pick the GARCH-benchmark path</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Package Legitimacy Audit lines 117-126; Critical Finding 6 lines 584-594 — arch missing + fallback)
    - benchmark/pyproject.toml (comparison dependency group)
  </read_first>
  <action>
Select the GARCH-benchmark reference path. The reference package `arch` is not yet in benchmark/.venv but is verified-legitimate (PyPI `arch`, maintainer Kevin Sheppard, Production/Stable, released continuously since 2014, latest 8.x — the de facto Python ARCH/GARCH library; T-03-SC accepted). Run `benchmark/.venv/bin/pip index versions arch` (or `cd benchmark && uv pip index versions arch`) to confirm the package resolves from the configured index and record the resolved version. If it resolves, Task 1 adds `arch>=5.3.0` and uses it as the GARCH reference. If the index lookup is unavailable/blocked, Task 1 uses the documented variance-convergence self-check fallback instead. Record the chosen path in a comment for Task 1. This is a legitimacy-verification step, not a gate — do not block the autonomous run (arch is well-established; either path satisfies the behavioral-parity criterion).
  </action>
  <verify>
    <automated>cd benchmark && .venv/bin/pip index versions arch 2>/dev/null | head -1 || echo "index lookup unavailable — use variance-convergence fallback in Task 1"</automated>
  </verify>
  <acceptance_criteria>
    - `arch` resolves from the index (a version is printed) → proceed with the arch path in Task 1.
    - If the index lookup is unavailable/blocked → Task 1 uses the documented variance-convergence fallback instead. Either outcome is acceptable; record which path was taken.
  </acceptance_criteria>
  <done>arch legitimacy confirmed (or fallback path selected); the choice is recorded for Task 1.</done>
</task>

<task type="auto">
  <name>Task 1: Three benchmarks (GARCH, Kalman, VAR) with committed results under benchmark/.venv</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Critical Finding 6 lines 584-633 — GARCH/Kalman/VAR benchmark references, arch missing, statsmodels UnobservedComponents + VAR available; Pattern 6 synthetic VAR(1) lines 831-857)
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (benchmark run.py template lines 508-538; No-Analog note on config shape lines 634-637 — read benchmark/configs/global_ets.py first)
    - benchmark/configs/global_ets.py (config shape: BENCHMARK_NAME, FUNCTION_NAME, MAX_SERIES, MODELS)
    - benchmark/m4/global_benchmark/run.py (the create_benchmark_functions harness usage)
    - benchmark/pyproject.toml (comparison dependency group location for arch)
    - .planning/STATE.md (Execution Notes Phase 1: benchmarks run under benchmark/.venv/bin/python, CLI subprocess via build/release/duckdb -unsigned to avoid venv/extension version mismatch)
  </read_first>
  <action>
Create three benchmark configs + run scripts mirroring benchmark/m4/global_benchmark/. All Python runs under benchmark/.venv/bin/python — never system python3.

1. If Task 0 confirmed arch: add `arch>=5.3.0` to the comparison dependency group in benchmark/pyproject.toml and run `cd benchmark && uv sync --extra comparison` (or the project's established venv-sync command). If Task 0 selected the fallback, skip this and implement the variance-convergence self-check in garch's run.py (compare anofox GARCH forecast variance against the analytical long-run variance ω/(1-α-β) and assert convergence + monotone approach; document this is a self-consistency check, not external parity).

2. benchmark/configs/garch.py, kalman.py, var.py: mirror global_ets.py shape. GARCH/Kalman use FUNCTION_NAME='TS_FORECAST_BY' with MODELS naming 'GARCH' / 'Kalman' (Kalman config includes both local_level and local_linear_trend params). VAR config points at ts_forecast_var_by with synthetic-data source (no M4).

3. benchmark/m4/garch_benchmark/run.py: reference = arch.arch_model on a returns-like series (M4 Daily first-differences or synthetic returns) OR the variance-convergence fallback. Commit results to results/.

4. benchmark/m4/kalman_benchmark/run.py: reference = statsmodels.tsa.statespace.structural.UnobservedComponents ('local level' and 'local linear trend'); behavioral/approximate parity (anofox uses fixed variance params, statsmodels MLE-estimates — do NOT assert exact numeric match). Commit results/.

5. benchmark/m4/var_benchmark/run.py: generate synthetic VAR(1) data in Python (generate_var1_data per RESEARCH Pattern 6: c=[0.5,0.3], a=[[0.6,0.1],[0.05,0.7]], N=200, seed=42); reference = statsmodels.tsa.api.VAR fit(maxlags=order, ic=None); compare against anofox ts_forecast_var_by via the CLI subprocess pattern (build/release/duckdb -unsigned). Parity criterion: forecast MAE close to the statsmodels reference on the same held-out steps; document the criterion. Commit results/.

Run all three; commit the results/*.parquet (or the committed result format the harness uses).
  </action>
  <verify>
    <automated>benchmark/.venv/bin/python benchmark/m4/var_benchmark/run.py --run && ls benchmark/m4/var_benchmark/results/ && ls benchmark/m4/kalman_benchmark/results/ && ls benchmark/m4/garch_benchmark/results/</automated>
  </verify>
  <acceptance_criteria>
    - All three benchmark run.py scripts execute under benchmark/.venv/bin/python with exit 0.
    - benchmark/m4/{garch,kalman,var}_benchmark/results/ each contain committed result files.
    - VAR benchmark compares anofox ts_forecast_var_by against statsmodels VAR on synthetic VAR(1) and reports a parity metric (MAE/coefficient recovery).
    - GARCH benchmark uses arch (if added) or the documented variance-convergence fallback — the chosen path is recorded in run.py comments.
    - No script imports or runs under system python3 (grep-verify the run commands use benchmark/.venv).
  </acceptance_criteria>
  <done>All three models have committed reference cross-check benchmarks; the arch gap is explicitly handled.</done>
</task>

<task type="auto">
  <name>Task 2: Docs for GARCH/Kalman/VAR + 07-forecasting.md sections, all SQL examples verified end-to-end</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Docs Layout lines 861-878 — new dirs classical/ and multivariate/, kalman in state-space/, extend 07-forecasting.md after Panel section)
    - .planning/phases/03-classical-multivariate-models/03-CONTEXT.md (GARCH volatility-not-variance doc must-have; Kalman kalman_model param; VAR value_cols LIST + long format + single-panel v1 + order param)
    - docs/api/07-forecasting.md (existing structure; Panel section ~line 318 as the insertion anchor)
    - docs/reference/models/state-space/ (existing page layout to match for kalman.md)
    - examples/forecasting/classical_forecasting_examples.sql (source the verified SQL snippets from here — do not invent new unverified SQL)
    - MEMORY.md feedback_verify_sql_docs (PR #230: run every doc snippet through the built extension)
  </read_first>
  <action>
Write the three model doc pages and extend the API forecasting doc. Every SQL snippet MUST be copied from (or verified identical to) examples/forecasting/classical_forecasting_examples.sql and then re-run against the built extension — no eyeballing (PR #230).

1. docs/reference/models/classical/garch.md (new dir): GARCH(p,q) model; params garch_p/garch_q; default GARCH(1,1); min-obs = p+q+10; explicitly document forecast_value = conditional volatility (standard deviation) = sqrt(forecast_variance), NOT variance; note GARCH is designed for returns (first differences), not raw price levels (Pitfall 9). Runnable ts_forecast_by(...,'GARCH',...) example.

2. docs/reference/models/state-space/kalman.md: KalmanForecaster; params{'kalman_model': 'local_level'(default) | 'local_linear_trend'}; returns h-step point forecasts. Runnable ts_forecast_by(...,'Kalman',...) examples for both specs.

3. docs/reference/models/multivariate/var.md (new dir): VAR multivariate; value_cols VARCHAR[]; order param (default 1); LONG-format output {variable, forecast_step, forecast_date, forecast_value}; single-panel only in v1 (no group_col); point forecasts only (intervals deferred). Runnable ts_forecast_var_by(...) example.

4. docs/api/07-forecasting.md: add a "Classical Models" subsection (GARCH, Kalman) after the Panel section, and a "Multivariate" subsection (VAR). Cross-link the reference pages.

Re-run every SQL example in all four docs against the built extension (build/release/duckdb -unsigned, LOAD built extension). Fix any snippet that does not execute cleanly.
  </action>
  <verify>
    <automated>test -f docs/reference/models/classical/garch.md && test -f docs/reference/models/state-space/kalman.md && test -f docs/reference/models/multivariate/var.md && grep -qi 'volatility' docs/reference/models/classical/garch.md && grep -qi 'variance' docs/reference/models/classical/garch.md</automated>
  </verify>
  <acceptance_criteria>
    - The three doc pages exist; 07-forecasting.md has Classical + Multivariate subsections.
    - garch.md explicitly states forecast_value is volatility (std-dev), not variance (grep finds both terms in the volatility-vs-variance clarification).
    - Every SQL snippet in the four docs executes cleanly against the built extension (each extracted-and-run snippet exits 0) — record the verification run.
  </acceptance_criteria>
  <done>All three models are fully documented in docs/api/ and docs/reference/models/ with end-to-end-verified SQL examples; CLAS-01/02/03 DoD (example + docs + cross-check) is complete.</done>
</task>

<task type="auto">
  <name>Task 3: Update anofox-forecast-models SKILL.md with the GARCH/Kalman/VAR surface</name>
  <read_first>
    - .claude/skills/anofox-forecast-models/SKILL.md (existing 33-model surface, ts_forecast_by API section, param-surface tables — find where to add the new methods)
    - docs/reference/models/classical/garch.md, state-space/kalman.md, multivariate/var.md (the pages written in Task 2 — source of truth)
    - .planning/phases/03-classical-multivariate-models/03-CONTEXT.md (param keys, volatility-not-variance, VAR single-panel/long-format)
  </read_first>
  <action>
Update .claude/skills/anofox-forecast-models/SKILL.md to cover the three new models (project-instruction requirement: update the skill when the GARCH/Kalman/VAR surface ships). Add:
- GARCH to the ts_forecast_by method list with params garch_p/garch_q, and a clear note that forecast_value is volatility (std-dev), not variance, and that it needs p+q+10 minimum observations and is intended for returns.
- Kalman to the method list with params{'kalman_model':'local_level'|'local_linear_trend'}.
- A new section for ts_forecast_var_by (multivariate, distinct from ts_forecast_by): value_cols VARCHAR[], order param, LONG-format output, single-panel v1, point forecasts only.
Update the model count in the skill description if it states a specific number (33 → reflect the additions). Keep the entries terse and consistent with existing skill formatting.
  </action>
  <verify>
    <automated>grep -qi 'GARCH' .claude/skills/anofox-forecast-models/SKILL.md && grep -qi 'Kalman' .claude/skills/anofox-forecast-models/SKILL.md && grep -qi 'ts_forecast_var_by' .claude/skills/anofox-forecast-models/SKILL.md</automated>
  </verify>
  <acceptance_criteria>
    - SKILL.md mentions GARCH, Kalman, and ts_forecast_var_by.
    - The GARCH entry notes volatility-not-variance and the min-obs constraint.
    - The VAR entry notes value_cols, order, long-format, single-panel v1.
  </acceptance_criteria>
  <done>The models skill reflects the new GARCH/Kalman/VAR surface so future work discovers them.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| pyproject.toml → venv install | Adding arch pulls a new package into benchmark/.venv |
| doc SQL snippet → built extension | Doc examples executed against the loaded extension |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-03-SC | Tampering | arch PyPI install (supply chain) | medium | mitigate | Task 0 legitimacy gate (advisory): arch verified legitimate (Kevin Sheppard, Production/Stable, since 2014); confirm resolution via pip index versions before adding; fallback = no external dep (variance-convergence self-check) |
| T-03-08 | Information Disclosure | benchmark result files | low | accept | Synthetic/M4 public data only; no PII or secrets in committed results |
| T-03-09 | Tampering | unverified doc SQL misleading users | low | mitigate | PR #230 rule: every doc snippet re-run against the built extension before commit |
</threat_model>

<verification>
- All three benchmark run.py scripts execute under benchmark/.venv/bin/python; results/ committed for each.
- VAR benchmark reports parity against statsmodels VAR on synthetic VAR(1); Kalman against UnobservedComponents; GARCH against arch or the documented fallback.
- Three doc pages exist + 07-forecasting.md Classical/Multivariate sections; every SQL snippet verified end-to-end.
- garch.md documents volatility-not-variance; SKILL.md updated.
</verification>

<success_criteria>
- Roadmap success criterion 3: VAR benchmark parity confirmed on synthetic VAR(1).
- Roadmap success criterion 4: GARCH, Kalman, VAR each documented in docs/api/ + docs/reference/models/ and cross-checked in benchmark/.
- CLAS-01/02/03 reach full milestone Definition of Done (example + docs + reference cross-check + delivery pattern).
</success_criteria>

<output>
Create `.planning/phases/03-classical-multivariate-models/03-03-SUMMARY.md` when done.
</output>
