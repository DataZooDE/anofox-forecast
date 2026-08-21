---
phase: 02-global-panel-models
plan: 2
type: execute
wave: 2
depends_on: [02-1]
files_modified:
  - crates/anofox-fcst-ffi/src/lib.rs
  - examples/forecasting/global_panel_forecasting_examples.sql
  - docs/reference/models/exponential-smoothing/global_ets.md
  - docs/reference/models/theta/global_theta.md
  - docs/reference/models/intermittent/global_croston.md
  - docs/api/07-forecasting.md
  - .claude/skills/anofox-forecast-models/SKILL.md
autonomous: true
requirements: [GLOB-02, GLOB-03]
estimate:
  tokens: 90000
  raw_tokens: 45000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "ts_forecast_panel_by(..., 'GlobalTheta', ...) returns per-series point forecasts for a grouped panel (GLOB-02, D-Area1)"
    - "ts_forecast_panel_by(..., 'GlobalCroston', ...) returns per-series flat forecasts on an intermittent panel; croston_variant param selects Classic (default) or SBA (GLOB-03, D-Area1/D-Area2)"
    - "GlobalTheta and GlobalCroston reuse the exact same align->fit-once->emit-many contract proven by the GlobalETS tracer (no per-series loop)"
    - "The example file runs all three methods clean against the built extension; each method is documented in docs/reference/models and the docs/api forecasting page (D-Area4, PR #230 rule)"
  artifacts:
    - crates/anofox-fcst-ffi/src/lib.rs
    - examples/forecasting/global_panel_forecasting_examples.sql
    - docs/reference/models/exponential-smoothing/global_ets.md
    - docs/reference/models/theta/global_theta.md
    - docs/reference/models/intermittent/global_croston.md
    - docs/api/07-forecasting.md
  key_links:
    - "forecast_panel_impl match arms: GlobalTheta -> GlobalTheta::new().fit/predict; GlobalCroston -> GlobalCroston::new()/sba().fit/predict"
    - "croston_variant param (C++ bind) -> variant C-string arg -> CrostonVariant::{Classic,SBA}"
---

<objective>
Expand the proven GlobalETS panel slice to the two remaining methods — GlobalTheta and GlobalCroston — which share the same align->fit-once->emit-many FFI/C++/macro contract, then complete the documentation (per-model reference pages + docs/api forecasting section) and extend the runnable example to cover all three methods verified end-to-end.

Purpose: Deliver GLOB-02 and GLOB-03 by adding two match arms to the panel FFI (the C++/macro/registration layers already handle any method string), and satisfy success criterion 4 (docs + verified examples).
Output: `ts_forecast_panel_by` supports GlobalETS/GlobalTheta/GlobalCroston; three model reference docs + a docs/api panel section; the example file exercises all three.
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
@.planning/phases/02-global-panel-models/02-1-SUMMARY.md
</context>

<artifacts_this_phase_produces>
New symbols introduced by this plan (exclude from drift verification):
- FFI match arms (in existing anofox_ts_forecast_panel): method "GlobalTheta", method "GlobalCroston"; croston_variant handling
- New param MAP key already reserved in 02-1 bind: `croston_variant` ('Classic' default | 'SBA')
- New docs: docs/reference/models/exponential-smoothing/global_ets.md, docs/reference/models/theta/global_theta.md, docs/reference/models/intermittent/global_croston.md; a panel section in docs/api/07-forecasting.md
- Extended: examples/forecasting/global_panel_forecasting_examples.sql (Theta + Croston sections), .claude/skills/anofox-forecast-models/SKILL.md (panel surface entry)
No new FFI export, no new struct, no new C++ file, no new macro — all reused from 02-1.
</artifacts_this_phase_produces>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add GlobalTheta + GlobalCroston FFI match arms (share the align->fit->predict contract)</name>
  <files>crates/anofox-fcst-ffi/src/lib.rs</files>
  <read_first>
    - crates/anofox-fcst-ffi/src/lib.rs (the anofox_ts_forecast_panel + forecast_panel_impl added in 02-1 — the match on method is where the arms go; and the panel_ffi_tests module)
    - .planning/phases/02-global-panel-models/02-RESEARCH.md (Research Target 1: GlobalTheta::new()/fit/predict — no period; GlobalCroston::new()/sba()/with_variant + CrostonVariant enum; Pitfall 2 all-zero panel; re-export paths)
    - .planning/phases/02-global-panel-models/02-1-SUMMARY.md (exact function/impl names produced by 02-1)
  </read_first>
  <behavior>
    Extend panel_ffi_tests with:
    - Test 4 (GlobalTheta): a 3-series equal-length panel, method="GlobalTheta", horizon=4 -> Ok, shape [3][4], all finite.
    - Test 5 (GlobalCroston Classic): a 3-series intermittent panel (mostly zeros, >=2 demands in at least one series), method="GlobalCroston", variant=None -> Ok, shape [3][4], all values >= 0 and finite; Croston is flat so all 4 horizon values per series are equal.
    - Test 6 (GlobalCroston SBA): same panel, variant=Some("SBA") -> Ok; SBA forecast <= Classic forecast for the same series (SBA multiplies by 1 - alpha/2).
  </behavior>
  <action>
    Add imports `use anofox_forecast::models::theta::GlobalTheta;` and `use anofox_forecast::models::intermittent::{GlobalCroston, CrostonVariant};` to lib.rs.
    In `forecast_panel_impl`, add match arms alongside the existing "GlobalETS":
    - "GlobalTheta" => `let mut m = GlobalTheta::new(); m.fit(&panel)?; Ok(m.predict(horizon))` (no period; equal-length panel already guaranteed by C++ alignment).
    - "GlobalCroston" => select variant from the `variant` arg: `let m0 = if variant == Some("SBA") { GlobalCroston::sba() } else { GlobalCroston::new() }; let mut m = m0; m.fit(&panel)?; Ok(m.predict(horizon))`. (Use `GlobalCroston::with_variant(CrostonVariant::SBA)` if `sba()` is not the exact constructor — match the verified crate API from RESEARCH Target 1.)
    Keep the fallback arm returning the invalid-method error. Do NOT change the FFI signature, the struct, or the C++ side — the `variant` C-string arg and `croston_variant` param key were already plumbed in 02-1. Set model_name in the FFI wrapper from the method string (not hardcoded 'GlobalETS') so the emitted model_name column reflects the actual method — if 02-1 hardcoded it, change the wrapper to copy `method_str` into model_name (null-padded to 64).
    Note Pitfall 2: if GlobalCroston::fit returns Err for an all-zero panel (no series with >=2 demands), propagate it as ComputationError with the crate's message (do not swallow) — the C++ layer turns it into a clear DuckDB error.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && cargo test -p anofox-fcst-ffi panel_ffi 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `cargo test -p anofox-fcst-ffi panel_ffi` passes all six tests (Tests 1-3 from 02-1 plus 4-6 here).
    - `grep -c "GlobalTheta::new" crates/anofox-fcst-ffi/src/lib.rs` >= 1 and `grep -c "GlobalCroston" crates/anofox-fcst-ffi/src/lib.rs` >= 1.
    - The model_name written into PanelForecastResult equals the requested method string (Theta run reports 'GlobalTheta', Croston run reports 'GlobalCroston').
    - No change to the `anofox_ts_forecast_panel` signature or `PanelForecastResult` fields (grep the signature line is byte-identical to 02-1's, aside from body).
  </acceptance_criteria>
  <done>The panel FFI dispatches all three methods; unit tests confirm Theta and Croston (Classic + SBA) produce correctly-shaped finite forecasts.</done>
</task>

<task type="auto">
  <name>Task 2: Extend the runnable example to GlobalTheta + GlobalCroston — verified end-to-end</name>
  <files>examples/forecasting/global_panel_forecasting_examples.sql</files>
  <read_first>
    - examples/forecasting/global_panel_forecasting_examples.sql (the GlobalETS file from 02-1, including the `-- [02-2] ...` marker to replace)
    - .planning/phases/02-global-panel-models/02-RESEARCH.md (Code Examples: GlobalTheta call, GlobalCroston with croston_variant := 'SBA')
  </read_first>
  <action>
    Replace the `-- [02-2] GlobalTheta + GlobalCroston sections appended here` marker with:
    - Section 3 (GlobalTheta): `SELECT * FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 14, '1d') ORDER BY product_id, forecast_step;` — comment that Theta needs no seasonal_period.
    - Section 4 (GlobalCroston): create an intermittent panel (mostly zeros with occasional demand across >=3 series), then `SELECT * FROM ts_forecast_panel_by('panel_intermittent', item_id, ds, qty, 'GlobalCroston', 6, '1d', MAP{'croston_variant': 'SBA'}) ORDER BY item_id, forecast_step;` plus a Classic variant call for contrast.
    - Section 5 (optional): a small comparison SELECT unioning GlobalETS vs GlobalTheta forecasts for the same panel to show the surface is method-swappable.
    Every SELECT must return rows against the built extension (the extension already includes all three methods after Task 1 rebuild).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && cmake --build build/release --target anofox_forecast_loadable_extension 2>&1 | tail -5 && ./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql 2>&1 | tail -40</automated>
  </verify>
  <acceptance_criteria>
    - The example file runs clean (exit 0) against the rebuilt extension; the GlobalTheta and GlobalCroston sections each return rows (no error, no empty result).
    - GlobalCroston output is non-negative and flat per series (all `horizon` values equal for a given item).
    - `grep -c "GlobalTheta" examples/forecasting/global_panel_forecasting_examples.sql` >= 1 and `grep -c "GlobalCroston" examples/forecasting/global_panel_forecasting_examples.sql` >= 1.
    - The `-- [02-2] ...` placeholder marker is gone (replaced by real sections).
  </acceptance_criteria>
  <done>The single example file demonstrates all three panel methods and passes end-to-end against the built extension — GLOB-02 and GLOB-03 verified.</done>
</task>

<task type="auto">
  <name>Task 3: Documentation — three model reference pages + docs/api panel section + skill update</name>
  <files>docs/reference/models/exponential-smoothing/global_ets.md, docs/reference/models/theta/global_theta.md, docs/reference/models/intermittent/global_croston.md, docs/api/07-forecasting.md, .claude/skills/anofox-forecast-models/SKILL.md</files>
  <read_first>
    - docs/reference/models/theta/auto_theta.md (per-model doc template: Signature / Description / Parameters table / Returns table / SQL Example / Best For)
    - docs/reference/models/intermittent/croston_sba.md (intermittent-model doc phrasing to mirror for GlobalCroston)
    - docs/api/07-forecasting.md (existing ts_forecast_by section — add a sibling panel section, matching heading depth and prose style)
    - .claude/skills/anofox-forecast-models/SKILL.md (where the model surface is catalogued — add the panel surface + three Global* methods)
    - examples/forecasting/global_panel_forecasting_examples.sql (use the verified SQL snippets in the docs so doc examples match reality)
  </read_first>
  <action>
    Create three model reference pages following the auto_theta.md template. Each documents the model via the panel surface `ts_forecast_panel_by`:
    - docs/reference/models/exponential-smoothing/global_ets.md — GlobalETS: cross-series pooled ETS (GlobalAutoETS, ModelPool::Reduced default). Parameters table: method 'GlobalETS', horizon, frequency, params keys `seasonal_period` (default 0), `model_pool` ('Reduced' default | 'Complete'). Returns: {group_col, forecast_step, date_col, yhat, model_name}. Best For: many related series with shared seasonal dynamics. Note point-forecasts-only (intervals via conformal path, deferred — D-Area3).
    - docs/reference/models/theta/global_theta.md — GlobalTheta: pooled Theta, no seasonal_period. Same returns table. Best For: many trended series, minimal config.
    - docs/reference/models/intermittent/global_croston.md — GlobalCroston: pooled Croston, params key `croston_variant` ('Classic' default | 'SBA'). Best For: intermittent/spare-parts panels. Note flat forecast + non-negativity.
    Use the exact verified SQL snippets from the example file in each doc's SQL Example section.
    Edit docs/api/07-forecasting.md: add a "Panel / Global forecasting (`ts_forecast_panel_by`)" section after the `ts_forecast_by` section, covering the fit-once-emit-many concept, the full signature `ts_forecast_panel_by(source, group_col, date_col, target_col, method, horizon, frequency, params := MAP{})`, the three methods, ragged auto-alignment + drop-with-surfacing behavior, and the point-forecasts-only + intervals-deferred note (D-Area3). Link to the three reference pages.
    Edit .claude/skills/anofox-forecast-models/SKILL.md: add the panel surface and the three Global* methods to the model catalogue / API surface section so the skill stays accurate.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && test -f docs/reference/models/exponential-smoothing/global_ets.md && test -f docs/reference/models/theta/global_theta.md && test -f docs/reference/models/intermittent/global_croston.md && grep -l "ts_forecast_panel_by" docs/api/07-forecasting.md .claude/skills/anofox-forecast-models/SKILL.md && echo DOCS_OK</automated>
  </verify>
  <acceptance_criteria>
    - The three model reference files exist and each contains a `ts_forecast_panel_by` SQL example matching the verified example file (grep each for `ts_forecast_panel_by`).
    - docs/api/07-forecasting.md contains a panel section referencing `ts_forecast_panel_by` and all three method names ('GlobalETS','GlobalTheta','GlobalCroston').
    - .claude/skills/anofox-forecast-models/SKILL.md references `ts_forecast_panel_by`.
    - Verify command prints DOCS_OK.
    - Any SQL snippet embedded in docs is copied from the end-to-end-verified example (no invented signatures) — per the verify-SQL-docs lesson.
  </acceptance_criteria>
  <done>Every Global* model is documented in docs/reference/models and the docs/api forecasting page; the models skill reflects the new panel surface. Success criterion 4 (docs) is met.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL user data -> C++ table function -> Rust FFI | Same boundary as 02-1; this plan only adds Rust match arms, no new crossing shape |
| Docs -> user | Documentation examples must reflect the real, verified surface (stale docs mislead but are not a security threat) |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-02-06 | Denial of Service | GlobalCroston fit Err on all-zero intermittent panel (RESEARCH Pitfall 2) | low | mitigate | Propagate ForecastError -> ComputationError -> clear DuckDB exception; do not swallow or hang; example uses a panel with real demands |
| T-02-07 | Tampering | Rust panic in Theta/Croston fit crossing FFI | medium | mitigate | Covered by the existing catch_unwind in anofox_ts_forecast_panel (02-1); new arms run inside forecast_panel_impl which is inside catch_unwind |
| T-02-08 | Repudiation | Doc examples drift from the shipped surface | low | mitigate | Doc SQL copied verbatim from the end-to-end-verified example file (verify-SQL-docs lesson / PR #230) |
</threat_model>

<verification>
- `cargo test -p anofox-fcst-ffi panel_ffi` — six tests green (Task 1).
- Example file runs clean covering all three methods against the rebuilt extension (Task 2).
- Three reference docs exist + docs/api panel section + skill updated (Task 3).
- Reused-contract check: no change to `anofox_ts_forecast_panel` signature / `PanelForecastResult` / the C++ file / the macro.
</verification>

<success_criteria>
- GLOB-02 and GLOB-03 satisfied: GlobalTheta and GlobalCroston (Classic + SBA) forecast a grouped panel via `ts_forecast_panel_by`, verified end-to-end.
- Success criterion 4 (docs + verified examples) met for all three Global* models.
- The two new methods reuse the tracer's align->fit-once->emit-many contract with zero new ABI surface.
</success_criteria>

<output>
Create `.planning/phases/02-global-panel-models/02-2-SUMMARY.md` when done.
</output>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && cmake --build build/release --target anofox_forecast_loadable_extension 2>&1 | tail -5 && ./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql 2>&1 | tail -40