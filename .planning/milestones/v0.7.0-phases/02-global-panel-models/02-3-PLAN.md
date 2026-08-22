---
phase: 02-global-panel-models
plan: 3
type: execute
wave: 3
depends_on: [02-2]
files_modified:
  - benchmark/configs/global_ets.py
  - benchmark/configs/statsforecast_global.py
  - benchmark/src/common/anofox_runner.py
  - benchmark/m4/global_benchmark/run.py
  - benchmark/m4/global_benchmark/results/.gitkeep
autonomous: true
requirements: [GLOB-01, GLOB-02, GLOB-03]
estimate:
  tokens: 78000
  raw_tokens: 39000
  tasks: 2
  confidence: low
must_haves:
  truths:
    - "A benchmark runner drives ts_forecast_panel_by over the existing M4 subset for GlobalETS/GlobalTheta/GlobalCroston and writes committed parquet results under benchmark/m4/global_benchmark/results/ (D-Area4, success criterion 3)"
    - "Each global model's accuracy is compared to a statsforecast reference and meets the behavioral/approximate parity criterion (relative MASE within tolerance), matching the Phase 1 cross-check standard (D-Area4)"
    - "All benchmark/cross-check scripts run under benchmark/.venv/bin/python (or `cd benchmark && uv run python ...`), NOT system python3 (STATE Execution Notes, project rule)"
  artifacts:
    - benchmark/configs/global_ets.py
    - benchmark/configs/statsforecast_global.py
    - benchmark/m4/global_benchmark/run.py
    - benchmark/m4/global_benchmark/results/
  key_links:
    - "global_benchmark/run.py -> create_benchmark_functions(global_ets, statsforecast_global) -> anofox panel runner (ts_forecast_panel_by) + statsforecast reference -> evaluate -> parquet"
    - "anofox_runner panel variant substitutes TS_FORECAST_PANEL_BY for the per-series TS_FORECAST_BY query shape"
---

<objective>
Prove statsforecast parity for the three Global* panel models on the existing M4 subset and commit the results — closing phase success criterion 3. Reuses the `create_benchmark_functions` factory and the M4 data already under `benchmark/m4/`; the only genuinely new logic is a panel query variant in the anofox runner (calling `ts_forecast_panel_by` instead of the per-series `ts_forecast_by`) and two config modules.

Purpose: Deliver the committed benchmark evidence (success criterion 3) that each global model reaches behavioral/approximate MASE parity with a statsforecast reference — the same tolerance standard adopted for the Phase 1 ADF cross-check (D-Area4).
Output: benchmark/m4/global_benchmark/ with run.py, configs, and committed parquet results showing anofox-vs-statsforecast parity for GlobalETS/GlobalTheta/GlobalCroston.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/02-global-panel-models/02-CONTEXT.md
@.planning/phases/02-global-panel-models/02-RESEARCH.md
@.planning/phases/02-global-panel-models/02-PATTERNS.md
@.planning/phases/02-global-panel-models/02-2-SUMMARY.md
</context>

<artifacts_this_phase_produces>
New symbols/files introduced by this plan (exclude from drift verification):
- benchmark/configs/global_ets.py (anofox model config: GlobalETS/GlobalTheta/GlobalCroston)
- benchmark/configs/statsforecast_global.py (statsforecast reference config)
- benchmark/m4/global_benchmark/run.py (fire entry point via create_benchmark_functions)
- benchmark/m4/global_benchmark/results/*.parquet (committed anofox + statsforecast + metrics outputs)
- A panel-query variant in benchmark/src/common/anofox_runner.py (new function or a function_name parameter defaulting to the existing per-series behavior — additive, non-breaking)
Depends on 02-2: the built extension must already expose ts_forecast_panel_by with all three methods.
</artifacts_this_phase_produces>

<tasks>

<task type="auto">
  <name>Task 1: Panel benchmark runner + configs (anofox panel query variant + statsforecast reference)</name>
  <files>benchmark/configs/global_ets.py, benchmark/configs/statsforecast_global.py, benchmark/src/common/anofox_runner.py, benchmark/m4/global_benchmark/run.py, benchmark/m4/global_benchmark/results/.gitkeep</files>
  <read_first>
    - benchmark/m4/ets_benchmark/run.py (verbatim structure for the fire entry point + create_benchmark_functions call)
    - benchmark/configs/ets.py and benchmark/configs/statsforecast_ets.py (config module shape: BENCHMARK_NAME, MODELS list, params lambdas)
    - benchmark/src/common/benchmark_runner.py (create_benchmark_functions factory signature — anofox_config, statsforecast_config, output_dir)
    - benchmark/src/common/anofox_runner.py:135-148 (the hardcoded TS_FORECAST_BY query — add a panel variant or a function_name param)
    - benchmark/src/common/statsforecast_runner.py and evaluation.py (reference model invocation + metric computation, incl. MASE)
    - .planning/phases/02-global-panel-models/02-RESEARCH.md (Research Target 6: config templates, panel query shape, statsforecast reference models GlobalETS / Theta / CrostonOptimized-ADIDA, venv rule)
  </read_first>
  <action>
    Create benchmark/configs/global_ets.py mirroring ets.py: `BENCHMARK_NAME = 'global_ets'`; `MODELS = [{'name':'GlobalETS','params': lambda seasonality: {'seasonal_period': seasonality}}, {'name':'GlobalTheta','params': lambda seasonality: {}}, {'name':'GlobalCroston','params': lambda seasonality: {}}]`.
    Create benchmark/configs/statsforecast_global.py mirroring statsforecast_ets.py: reference models from statsforecast that best approximate each global model — GlobalETS -> statsforecast `AutoETS` (or `GlobalETS` if available in the pinned version), GlobalTheta -> `AutoTheta`/`Theta`, GlobalCroston -> `CrostonOptimized` (or `ADIDA`). Verify the exact importable class names against the pinned statsforecast in the venv (see verify command) and use what exists; document the chosen reference per model in a comment (behavioral/approximate parity, not identical algorithm — D-Area4).
    Modify benchmark/src/common/anofox_runner.py additively: add a panel query path. Preferred: add a `function_name='TS_FORECAST_BY'` parameter (default preserves existing per-series behavior for all Phase 1 benchmarks) and, when `function_name='TS_FORECAST_PANEL_BY'`, emit the panel query shape from RESEARCH Target 6 (`SELECT * FROM TS_FORECAST_PANEL_BY('train', unique_id, ds, y, '{model}', {horizon}, '{freq}', {map_literal})`) — a single call over the whole panel, not a per-series GROUP BY. Keep the CLI-subprocess-to-duckdb approach used in Phase 1 (STATE decision: CLI subprocess avoids the venv duckdb v1.5.1 vs extension v1.5.4 mismatch). Do NOT change the default call path used by existing benchmarks.
    Create benchmark/m4/global_benchmark/run.py mirroring ets_benchmark/run.py: `sys.path.insert` to benchmark root; `from src.common.benchmark_runner import create_benchmark_functions`; `from configs import global_ets, statsforecast_global`; wire the anofox side to use the panel function variant; `fire.Fire({'run':run,'anofox':anofox,'statsforecast':statsforecast,'evaluate':evaluate})`. Header comment must state the run command uses the venv: `cd benchmark && uv run python m4/global_benchmark/run.py run`.
    Create benchmark/m4/global_benchmark/results/.gitkeep so the results dir is tracked before parquet lands (Task 2 fills it).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast/benchmark && ./.venv/bin/python -c "import statsforecast, importlib; from configs import global_ets, statsforecast_global; print('configs_ok', global_ets.BENCHMARK_NAME, [m['name'] for m in global_ets.MODELS])" 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - benchmark/configs/global_ets.py imports and exposes `BENCHMARK_NAME == 'global_ets'` and a MODELS list naming exactly 'GlobalETS','GlobalTheta','GlobalCroston'.
    - benchmark/configs/statsforecast_global.py imports without error under `benchmark/.venv/bin/python` and every referenced statsforecast class actually exists in the pinned version (the import line in verify does not raise).
    - benchmark/m4/global_benchmark/run.py exists and its header documents the `cd benchmark && uv run python ...` (venv) run command.
    - anofox_runner.py change is additive: existing benchmarks' default path is unchanged (default `function_name='TS_FORECAST_BY'`); grep confirms a `TS_FORECAST_PANEL_BY` branch exists.
    - The verify command prints `configs_ok global_ets ['GlobalETS', 'GlobalTheta', 'GlobalCroston']`.
  </acceptance_criteria>
  <done>The panel benchmark harness is wired: configs load under the venv, the anofox runner can drive ts_forecast_panel_by, and run.py is ready to execute over the M4 subset.</done>
</task>

<task type="auto">
  <name>Task 2: Run the parity benchmark and commit results (behavioral MASE parity)</name>
  <files>benchmark/m4/global_benchmark/results/.gitkeep</files>
  <read_first>
    - benchmark/m4/ets_benchmark/results/ (naming convention of committed parquet outputs: anofox-<name>-<freq>.parquet, anofox-<name>-<freq>-metrics.parquet, statsforecast-<model>-<freq>.parquet)
    - benchmark/src/common/evaluation.py (MASE + metric column names to interpret parity)
    - .planning/phases/02-global-panel-models/02-RESEARCH.md (Assumption A3: statsforecast GlobalETS pooling may differ -> use within-5%-MASE behavioral tolerance; Target 6 results file names)
    - .planning/STATE.md:88-90 (venv rule; Phase 1 CLI-subprocess decision)
  </read_first>
  <action>
    Run the benchmark end-to-end under the venv against the M4 subset already present under benchmark/m4/ (reuse it, do not download new data): `cd benchmark && uv run python m4/global_benchmark/run.py run`. This produces, for each frequency present in the subset, the anofox panel forecasts, the statsforecast reference forecasts, and the evaluation metrics as parquet under benchmark/m4/global_benchmark/results/.
    Inspect the metrics parquet and confirm the behavioral/approximate parity criterion: each global model's MASE is within the adopted tolerance (target: within ~5% relative MASE of its statsforecast reference, matching the Phase 1 ADF behavioral-cross-check standard — D-Area4). Because global pooling differs from statsforecast's per-series fits (RESEARCH Assumption A3), parity is behavioral, not exact; if a model is outside tolerance, record the observed gap and the reference chosen in the SUMMARY rather than forcing exact numbers — but the runner must complete and emit results for all three methods.
    Commit the resulting parquet files under benchmark/m4/global_benchmark/results/ (the .gitkeep can be removed once real parquet is committed, or kept — either is fine). Do NOT commit the M4 raw data or the venv.
    Record in the plan SUMMARY: the exact parity numbers per model (anofox MASE vs statsforecast MASE and the relative gap), and the statsforecast reference class used for each.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && ls benchmark/m4/global_benchmark/results/*.parquet 2>&1 && benchmark/.venv/bin/python -c "import glob,pandas as pd; fs=sorted(glob.glob('benchmark/m4/global_benchmark/results/*metrics*.parquet')); assert fs, 'no metrics parquet'; [print(f, pd.read_parquet(f).to_dict('records')) for f in fs]" 2>&1 | tail -40</automated>
  </verify>
  <acceptance_criteria>
    - `benchmark/m4/global_benchmark/results/` contains committed parquet files: anofox forecasts, statsforecast reference forecasts, and a metrics parquet — for each of GlobalETS, GlobalTheta, GlobalCroston (naming mirrors ets_benchmark/results/).
    - The metrics parquet loads and contains a MASE (or equivalent) column per model for both anofox and statsforecast.
    - Each model's anofox MASE is within the adopted behavioral tolerance of its statsforecast reference (target ~5% relative), OR the observed gap + reference class is explicitly recorded in the SUMMARY with justification (pooling differs — RESEARCH A3).
    - The benchmark was run under `benchmark/.venv` (not system python3) — the run.py header documents this and results were produced by `uv run`.
    - The verify command lists parquet files and prints metrics records without raising.
  </acceptance_criteria>
  <done>Committed benchmark results demonstrate behavioral parity for all three Global* models against statsforecast on the M4 subset — success criterion 3 is met.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| Benchmark script -> built extension (CLI subprocess) | The runner shells into the duckdb CLI to avoid the venv/extension version mismatch; input is the local M4 subset, no network |
| statsforecast (venv) -> results parquet | Reference model runs locally under the pinned venv; no external service |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-02-09 | Tampering | Wrong python interpreter (system vs venv) yields wrong/no statsforecast results | medium | mitigate | All commands use benchmark/.venv/bin/python or `uv run`; run.py header enforces it (STATE Execution Notes) |
| T-02-10 | Repudiation | Committed parquet results not reproducible / stale vs shipped extension | low | mitigate | Benchmark runs against the just-built extension from 02-2 via CLI subprocess; SUMMARY records the extension build + exact parity numbers |
| T-02-SC | Tampering | New python package installs (supply chain) | low | accept | No new packages — statsforecast/pandas already in the pinned benchmark venv (RESEARCH Package Legitimacy Audit: no new packages); nothing to install |
</threat_model>

<verification>
- Configs load under the venv; anofox runner has an additive panel path (Task 1).
- `cd benchmark && uv run python m4/global_benchmark/run.py run` produces committed parquet results for all three methods (Task 2).
- Metrics parquet shows anofox-vs-statsforecast MASE within the behavioral tolerance (Task 2).
- venv rule honored throughout (no system python3).
</verification>

<success_criteria>
- Success criterion 3 satisfied: committed benchmark results under benchmark/ show statsforecast parity for GlobalETS, GlobalTheta, and GlobalCroston.
- Parity uses the behavioral/approximate MASE tolerance standard (D-Area4), consistent with the Phase 1 cross-check precedent.
- No new dependencies; all benchmarking runs under benchmark/.venv.
</success_criteria>

<output>
Create `.planning/phases/02-global-panel-models/02-3-SUMMARY.md` when done.
</output>