# Phase 2: Global / Panel Models - Context

**Gathered:** 2026-08-21
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous) — 16 decisions across 4 areas, all recommendations accepted

<domain>
## Phase Boundary

Expose the upstream crate's cross-series global learners — **GlobalETS**, **GlobalTheta**, **GlobalCroston** (`anofox-forecast 0.15.3`, `crate::batch` / `models::{exponential,theta,intermittent}`) — through a new **panel-aware SQL surface**. These models fit shared parameters across an entire panel at once (true cross-learning), then emit per-series forecasts. This is distinct from the per-series `ts_forecast_by` dispatch and requires a new FFI export, a new native table function, and a new macro.

Delivers requirements **GLOB-01** (GlobalETS), **GLOB-02** (GlobalTheta), **GLOB-03** (GlobalCroston).

In scope: point forecasts for a grouped panel via a single method-dispatched surface, ragged-panel alignment, statsforecast parity benchmark, docs + runnable example. Out of scope: prediction intervals (deferred to the existing conformal path), per-series fitted-spec metadata output.

</domain>

<decisions>
## Implementation Decisions

### Area 1 — Panel Forecast SQL Surface Shape
- **New dedicated surface**, not an extension of `ts_forecast_by`. The per-series dispatch is incompatible with fit-once-emit-many global models (confirmed by PROJECT.md + STATE.md design flag).
- Delivery: new FFI export (`crates/anofox-fcst-ffi`) → new native table function `_ts_forecast_panel_native` (`src/table_functions/`) → user-facing macro **`ts_forecast_panel_by`** (`src/macros/ts_macros.cpp`).
- Model selection via a **`method` string**: `'GlobalETS'`, `'GlobalTheta'`, `'GlobalCroston'` — mirrors `ts_forecast_by`.
- Signature **mirrors `ts_forecast_by`**: `ts_forecast_panel_by(source, group_col, date_col, target_col, method, horizon, frequency, params := MAP{})`.

### Area 2 — Ragged Panel Handling
- The crate requires **all series to have equal length** (`GlobalETS::fit(&[Vec<f64>])`, same for Theta/Croston). SQL panels are ragged, so alignment happens **inside the table function before the FFI call**.
- **Auto-align** every series to a shared date grid (union of dates across the panel, on the declared `frequency`).
- **Gap-fill / leading-fill** each series up to the common length (reuse the extension's existing gap-fill path; leading gaps forward/zero-filled as appropriate to the model).
- Series that are **too short or all-null are dropped with a surfaced warning**; the rest still forecast (do not fail the whole call).
- **Intra-series nulls are imputed** (interpolation) before the global fit — global models need dense `f64`. Exact imputation method is Claude's discretion, consistent with existing data-prep utilities.

### Area 3 — Output Shape & Intervals
- **Long format**: one row per (series, horizon step) — identical shape to `ts_forecast_by`.
- **Point forecasts only for v1.** `predict(horizon)` returns `Vec<Vec<f64>>` (points); prediction intervals are deferred to the existing conformal prediction path as a follow-up, not built into this surface.
- Output columns match the existing surface: `{group_col}, forecast_date, forecast_value, model`.
- **No per-series fitted-spec metadata** in the output for v1 — keep it lean.

### Area 4 — Benchmark & Docs
- Parity **baseline = statsforecast** (the existing benchmark harness already compares against it).
- **Reuse the M4 subset** already present under `benchmark/m4/`.
- Parity criterion is **behavioral / approximate** (relative MASE within tolerance) — same standard adopted for the Phase 1 ADF cross-check, not exact numeric parity.
- Docs delivered in **`docs/api/`** and **`docs/reference/models/`**, plus a runnable **`examples/*.sql`** snippet verified end-to-end against the built extension (per success criteria and the PR #230 rule).

### Claude's Discretion
- Exact imputation/interpolation algorithm for intra-series nulls.
- Minimum-series-length threshold for the drop-with-warning rule.
- Internal FFI marshalling shape for the ragged→dense panel (row offsets vs. equal-length matrix), provided the crate's equal-length contract is met.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ts_forecast_by` macro (`src/macros/ts_macros.cpp:568+`) and its native table function `_ts_forecast_native` (`src/table_functions/ts_forecast_native.cpp`) — the template for the new panel surface (group collection → FFI → long-format emit).
- Existing gap-fill / imputation data-prep utilities (`_ts_fill_gaps_native` and related) — reuse for the ragged-panel alignment step.
- Existing validity-bitmask → `Vec<Option<f64>>` marshalling at the FFI boundary — adapt for the dense-panel requirement.
- Benchmark harness under `benchmark/m4/` with statsforecast comparison scripts.

### Established Patterns
- Delivery pattern (locked): Rust FFI `#[no_mangle] pub extern "C"` export → C++ table function → registration in `src/anofox_forecast_extension.cpp` → `ts_*_by` macro → `examples/*.sql` → docs.
- DuckDB GROUP BY / scalar parallelism only — **no custom threading, no table-in/table-out** (project rule). The panel table function collects the whole panel in-memory in a Finalize barrier (same as existing native functions), fits once, emits.
- FFI panics caught via `catch_unwind`; errors mapped to DuckDB exceptions.

### Integration Points
- Upstream API (verified in `~/.cargo/registry/.../anofox-forecast-0.15.3`):
  - `models::exponential::global_ets::GlobalETS::{new(spec, period), fit(&[Vec<f64>]), predict(horizon) -> Vec<Vec<f64>>}`
  - `models::theta::global_theta::GlobalTheta` and `models::intermittent::global_croston::GlobalCroston` — analogous `new`/`fit`/`predict`.
  - `batch.rs` facade also offers `auto_ets`/`ets`/`mfles` (independent per-series fits with shared compute) — NOT the target here; the phase wants the true shared-parameter Global* learners.
  - **Hard contract:** all series passed to `fit` must be equal length (documented in `batch.rs` and enforced in `global_ets.rs`).
- New FFI export lands in `crates/anofox-fcst-ffi/src/lib.rs`; core wrapper (if needed) in `crates/anofox-fcst-core`.

</code_context>

<specifics>
## Specific Ideas

- Macro name is fixed to **`ts_forecast_panel_by`** (accepted over `ts_global_forecast_by` / `ts_panel_forecast_by`).
- Benchmark must produce committed results under `benchmark/` showing statsforecast parity for each of the three models (success criterion 3).
- Follow the Phase 1 precedent: cross-check scripts that need Python must run under the benchmark uv venv (`benchmark/.venv/bin/python`), not system python3.

</specifics>

<deferred>
## Deferred Ideas

- **Prediction intervals for global/panel forecasts** — route through the existing conformal prediction surface in a later increment rather than building interval logic into `ts_forecast_panel_by`.
- **Per-series fitted-spec / model-metadata output columns** — omitted from v1 to keep the output lean; revisit if users need model introspection.
- The `batch::auto_ets/ets/mfles` shared-compute per-series batch path (distinct from Global* cross-learning) — not exposed in this phase.

</deferred>
