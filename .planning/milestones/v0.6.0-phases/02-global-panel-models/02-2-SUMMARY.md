---
phase: 02-global-panel-models
plan: 2
subsystem: forecasting
tags: [rust-ffi, panel-forecasting, global-theta, global-croston, tdd, docs]

requires:
  - phase: 02-global-panel-models
    plan: 1
    provides: anofox_ts_forecast_panel FFI + PanelForecastResult + _ts_forecast_panel_native C++ + ts_forecast_panel_by macro (GlobalETS tracer)

provides:
  - GlobalTheta method arm in forecast_panel_impl (shared alpha via pooled Theta)
  - GlobalCroston method arm in forecast_panel_impl (Classic + SBA via sba() constructor)
  - variant_str parameter thread from FFI outer wrapper through to inner impl
  - model_name in PanelForecastResult now reflects actual method string (not hardcoded)
  - Examples sections 4-6 (GlobalTheta, GlobalCroston Classic+SBA, method comparison)
  - docs/reference/models/exponential-smoothing/global_ets.md
  - docs/reference/models/theta/global_theta.md
  - docs/reference/models/intermittent/global_croston.md
  - Panel section in docs/api/07-forecasting.md
  - ts_forecast_panel_by surface in .claude/skills/anofox-forecast-models/SKILL.md

affects:
  - 02-3 (if any): ts_forecast_panel_by is now stable for all 3 Global* methods
  - SKILL.md: updated with panel gotchas + all 3 Global* method signatures

actuals:
  tokens: 68000
  tasks: 3
  commits: 3

tech-stack:
  added:
    - GlobalTheta from anofox_forecast::models::theta (imported in anofox-fcst-ffi)
    - GlobalCroston from anofox_forecast::models::intermittent (imported in anofox-fcst-ffi)
  patterns:
    - Use GlobalCroston::new()/sba() constructors instead of with_variant() — CrostonVariant not re-exported at intermittent module level (global_croston::CrostonVariant != croston::CrostonVariant)
    - variant_str: Option<&str> added as 8th param to forecast_panel_impl (testable inner fn)
    - model_name now derived from the method string at call time, not hardcoded
    - TDD RED-GREEN applied: tests 4-6 written first (produced compile errors on signature change), implementation added second, all 6 tests green

key-files:
  modified:
    - crates/anofox-fcst-ffi/src/lib.rs (GlobalTheta + GlobalCroston match arms, variant_str param, model_name fix, tests 4-6)
    - examples/forecasting/global_panel_forecasting_examples.sql (sections 4-6 replacing placeholder)
    - docs/api/07-forecasting.md (new panel section)
    - .claude/skills/anofox-forecast-models/SKILL.md (panel surface entry)
  created:
    - docs/reference/models/exponential-smoothing/global_ets.md
    - docs/reference/models/theta/global_theta.md
    - docs/reference/models/intermittent/global_croston.md

key-decisions:
  - "Use GlobalCroston::new()/sba() instead of with_variant(CrostonVariant): global_croston::CrostonVariant and croston::CrostonVariant are two distinct types; the former is not re-exported by the intermittent mod.rs, causing E0308 type mismatch. The named constructors (new/sba) are the public API and compile correctly."
  - "Add variant_str: Option<&str> to forecast_panel_impl signature: needed to thread Croston variant from FFI outer wrapper into the inner function; updated all call sites (FFI wrapper + 3 tests that changed from 7-arg to 8-arg calls)"
  - "Fix model_name from hardcoded 'GlobalETS' to method_name.to_owned(): the PanelForecastResult.model_name was hardcoded to 'GlobalETS' in 02-1; now copies the actual method string so GlobalTheta/GlobalCroston runs report the correct method"
  - "TDD in single Rust file: RED tests added first (producing compile errors), GREEN implementation added to make them pass — standard TDD cycle applied within the constraints of a single-file crate"
  - "Build must use project duckdb binary (build/release/duckdb), not system duckdb: system binary is v1.5.5 while extension is built for v1.5.4; cmake --build target=duckdb builds a compatible binary"

requirements-completed: [GLOB-02, GLOB-03]

coverage:
  - id: D1
    description: "GlobalTheta panel forecast returns per-series forecasts via ts_forecast_panel_by"
    requirement: GLOB-02
    verification:
      - kind: unit
        ref: "panel_ffi_tests::test_global_theta_happy_path — 3 series × 4 steps, all finite"
        status: pass
      - kind: integration
        ref: "examples/forecasting/global_panel_forecasting_examples.sql Section 4 — 3 series × 14 steps = 42 rows, model_name='GlobalTheta'"
        status: pass
    human_judgment: false
  - id: D2
    description: "GlobalCroston Classic panel forecast: non-negative, flat per series"
    requirement: GLOB-03
    verification:
      - kind: unit
        ref: "panel_ffi_tests::test_global_croston_classic — non-negative, all steps equal per series"
        status: pass
      - kind: integration
        ref: "examples/forecasting/global_panel_forecasting_examples.sql Section 5 Classic — FLAT check all series"
        status: pass
    human_judgment: false
  - id: D3
    description: "GlobalCroston SBA forecast ≤ Classic (downward bias correction)"
    requirement: GLOB-03
    verification:
      - kind: unit
        ref: "panel_ffi_tests::test_global_croston_sba_le_classic — SBA ≤ Classic for all 3 series"
        status: pass
      - kind: integration
        ref: "examples/forecasting/global_panel_forecasting_examples.sql Section 5 SBA_LE_CLASSIC check"
        status: pass
    human_judgment: false
  - id: D4
    description: "Three model reference docs + API panel section + skill updated"
    requirement: GLOB-02
    verification:
      - kind: integration
        ref: "verify command: test -f global_ets.md && test -f global_theta.md && test -f global_croston.md && grep -l ts_forecast_panel_by 07-forecasting.md SKILL.md && echo DOCS_OK → DOCS_OK"
        status: pass
    human_judgment: false

duration: ~17 min
completed: 2026-08-21
status: complete
---

# Phase 02 Plan 2: GlobalTheta + GlobalCroston Panel Methods + Documentation Summary

**GlobalTheta and GlobalCroston added as match arms in forecast_panel_impl; all three Global* models verified end-to-end via ts_forecast_panel_by; three model reference docs + panel API section + skill update complete.**

## Performance

- **Duration:** ~17 min
- **Started:** 2026-08-21T19:47:47Z
- **Completed:** 2026-08-21T20:04:26Z
- **Tasks:** 3
- **Files modified:** 7 (1 Rust FFI, 1 SQL example, 3 docs created, 2 docs modified)

## Accomplishments

- **Task 1 (TDD):** Added `GlobalTheta::new()` and `GlobalCroston::new()/sba()` match arms to `forecast_panel_impl`. Added `variant_str: Option<&str>` parameter (threads from FFI wrapper to inner fn). Fixed `model_name` in `PanelForecastResult` from hardcoded `"GlobalETS"` to actual method string. All 6 `panel_ffi_tests` pass (Tests 1-3 from 02-1 + new Tests 4-6).

- **Task 2 (End-to-end verify):** Extended `global_panel_forecasting_examples.sql` with 3 new sections — GlobalTheta (Section 4: 42 rows, model_name='GlobalTheta'), GlobalCroston Classic+SBA (Section 5: flat-forecast check PASS, SBA≤Classic check PASS for all series), and method comparison (Section 6). Full example runs clean against built extension (`exit 0`).

- **Task 3 (Docs):** Created `docs/reference/models/exponential-smoothing/global_ets.md`, `docs/reference/models/theta/global_theta.md`, `docs/reference/models/intermittent/global_croston.md`. Added panel section to `docs/api/07-forecasting.md` covering fit-once-emit-many concept, full signature, all 3 methods, ragged alignment, drop rule, point-forecasts-only note, 4 verified SQL examples. Updated `SKILL.md` with `ts_forecast_panel_by` surface including 6 gotchas + quick examples.

## Task Commits

1. **Task 1: GlobalTheta + GlobalCroston FFI match arms** - `bae2302` (feat)
2. **Task 2: Extended panel example verified end-to-end** - `7660f15` (feat)
3. **Task 3: Three model docs + API panel section + skill update** - `1ee1595` (docs)

## Files Created/Modified

- `crates/anofox-fcst-ffi/src/lib.rs` — GlobalTheta/GlobalCroston match arms, variant_str param, model_name fix, Tests 4-6
- `examples/forecasting/global_panel_forecasting_examples.sql` — Sections 4-6 replacing placeholder
- `docs/reference/models/exponential-smoothing/global_ets.md` — (new) GlobalETS reference
- `docs/reference/models/theta/global_theta.md` — (new) GlobalTheta reference
- `docs/reference/models/intermittent/global_croston.md` — (new) GlobalCroston reference
- `docs/api/07-forecasting.md` — panel section added
- `.claude/skills/anofox-forecast-models/SKILL.md` — ts_forecast_panel_by surface added

## Decisions Made

- `GlobalCroston::new()/sba()` instead of `with_variant()`: `global_croston::CrostonVariant` is not re-exported by the `intermittent` module — only `croston::CrostonVariant` is. The two types are distinct, causing an E0308 type mismatch. The named constructors `new()` (Classic) and `sba()` (SBA) are the correct public API.
- `variant_str: Option<&str>` added to `forecast_panel_impl`: threads the Croston variant from the FFI outer function into the testable inner function. All existing test call sites updated to 8-arg form.
- `model_name` fixed from hardcoded `"GlobalETS"` to `method_name.to_owned()`: 02-1 hardcoded the method name; now the actual method string is used so `GlobalTheta` and `GlobalCroston` runs report their correct names.
- Build and test against project's `build/release/duckdb` (v1.5.4), not system `duckdb` (v1.5.5): version mismatch prevents loading extension with system binary.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `GlobalCroston::with_variant(CrostonVariant::SBA)` type mismatch**
- **Found during:** Task 1 (first cargo test run after implementing GREEN phase)
- **Issue:** `anofox_forecast::models::intermittent::CrostonVariant` (re-exported) is `croston::CrostonVariant`, but `GlobalCroston::with_variant()` expects `global_croston::CrostonVariant` (a private type in a private module). E0308 type mismatch.
- **Fix:** Use named constructors `GlobalCroston::new()` (Classic) and `GlobalCroston::sba()` (SBA) instead of `with_variant()`. Both are public API methods that produce the correct internal type.
- **Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
- **Committed in:** `bae2302` (Task 1)

**2. [Rule 1 - Bug] `model_name` hardcoded to "GlobalETS" in FFI wrapper**
- **Found during:** Task 1 (reviewing the FFI outer function for the model_name fix task requirement)
- **Issue:** The 02-1 implementation hardcoded `b"GlobalETS"` as the `model_name` in `PanelForecastResult` regardless of which method was called.
- **Fix:** Changed to use `method_name.to_owned()` (derived from parsing the `method` C-string), so any panel method (GlobalTheta, GlobalCroston) correctly names itself.
- **Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
- **Committed in:** `bae2302` (Task 1)

**3. [Rule 3 - Blocking] System duckdb binary (v1.5.5) incompatible with extension (v1.5.4)**
- **Found during:** Task 2 (first end-to-end test attempt)
- **Issue:** Extension was built against DuckDB v1.5.4 submodule; system `/home/simonm/.local/bin/duckdb` is v1.5.5. Version check fails with `Failed to load ... built specifically for DuckDB version 'v1.5.4'`.
- **Fix:** Built the `duckdb` target from the cmake build (`cmake --build build/release --target duckdb`) to produce a compatible v1.5.4 binary at `build/release/duckdb`.
- **Files modified:** None (build artifact)
- **Committed in:** Not committed (build artifact)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocking)
**Impact:** All fixes required for correctness. The type mismatch fix is a permanent improvement (correct Rust API usage). The model_name fix ensures GLOB-02/GLOB-03 emitted rows are correctly labeled. The build binary fix ensures PR #230 end-to-end verification is against a matching DuckDB version.

## Issues Encountered

- `CrostonVariant` type confusion: the crate has two separate enums with the same name in different modules (per-series Croston vs GlobalCroston) — only the per-series one is re-exported. The research note said "use `with_variant(CrostonVariant::SBA)`" which assumed re-export, but the named constructors (`sba()`, `new()`) are the correct approach.

## Self-Check: PASSED

- `bae2302`: exists in git log (feat: GlobalTheta + GlobalCroston FFI match arms)
- `7660f15`: exists in git log (feat: extend panel example)
- `1ee1595`: exists in git log (docs: three model docs + panel section + skill)
- `docs/reference/models/exponential-smoothing/global_ets.md`: file exists
- `docs/reference/models/theta/global_theta.md`: file exists
- `docs/reference/models/intermittent/global_croston.md`: file exists
- `panel_ffi_tests`: 6/6 passing
- Example SQL: exit 0, all sections return rows

## Next Phase Readiness

- Phase 02 plan 2 complete: GLOB-02 (GlobalTheta) and GLOB-03 (GlobalCroston) verified.
- No blockers for plan 3 (if any), or for final phase wrap-up.

---
*Phase: 02-global-panel-models*
*Completed: 2026-08-21*
