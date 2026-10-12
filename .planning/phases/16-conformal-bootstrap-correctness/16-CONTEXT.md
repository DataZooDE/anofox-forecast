# Phase 16: Conformal & Bootstrap Correctness - Context

**Gathered:** 2026-10-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Conformal and bootstrap intervals have their nominal coverage and correct shape end-to-end:
- horizon order and pairing are preserved in SQL;
- scores and empirical coverage are computed in Rust;
- per-group calibration is available;
- each conformal variant implements the method it is named after.

Requirements: RUST-04, CONF-01..CONF-05. Audit input: `.planning/research/STAT-AUDIT-2026-10-08.md` §D (D4–D6) and §G (G2, G12–G16).

Execution note: this phase is planned in parallel with Phase 15 but EXECUTES after Phase 15 completes, on the same main tree (worktrees disabled; serial execution by user decision 2026-10-12).

</domain>

<decisions>
## Implementation Decisions

### SQL contract (RUST-04)
- **D-01:** `ts_conformal_by`, `ts_conformal_apply_by` and `ts_interval_width_by` get a required date/step column parameter. The macros order and pair by it, and lower/upper stay paired per row: no independent sorting (D4, D5). This is a breaking signature change, documented in the CHANGELOG.
- **D-02:** `coverage` becomes the EMPIRICAL coverage on the calibration data, computed in Rust. A new `nominal_level` column carries the requested level. All residuals are computed in Rust, not SQL (D6).
- **D-03:** `ts_conformal_calibrate` calibrates per group by default, so groups with different residual scales get different quantiles. Pooled calibration happens only when explicitly requested via `params := {'pooled': true}`.
- **D-04:** Parallelism stays GROUP BY + scalar/aggregate, with no table-in/out functions (project rule).

### Split, asymmetric and adaptive semantics (CONF-02, CONF-03, CONF-04)
- **D-05:** When ⌈(n+1)(1−α)⌉ > n: NULL bounds and width (the "unbounded" interval in SQL) plus a `calibration_insufficient` indicator (or n reported). Never clamp to the max residual (G12).
- **D-06:** α ∉ (0,1) raises an error naming the value. NaN/NULL residuals are filtered before quantiles are taken, never a panic (G16), and the number of residuals used is reported as `n_calibration`.
- **D-07:** Asymmetric conformal takes its bounds from the α/2 and 1−α/2 quantiles of SIGNED residuals, with the same finite-sample correction. A seeded Monte Carlo test in Rust requires coverage within ±2pp of nominal on symmetric and skewed residuals (G13; it is about 40% too wide today).
- **D-08:** Adaptive conformal takes the difficulty d as an explicit per-row input (column/list) at both calibration and prediction. Scores are |r|/d and intervals q·d. There is no batch-mean normalisation, so a row's interval is independent of batch composition (G14).

### Jackknife+, bootstrap, testing and plan split (CONF-05, CONF-01)
- **D-09:** `'JackknifePlus'` is removed. All of its aliases (`jackknife+`, `jackknifeplus`, `jackknife_plus`, `jk+`) raise an error explaining it was split conformal and pointing to `'split'` / `'crossval'` (G15). A true jackknife+ needs leave-one-out refits that do not fit the GROUP BY architecture, so it is deferred.
- **D-10:** The bootstrap residual sign is fixed (G2). A seeded right-skewed (log-normal) residual fixture must put the long tail on the upper side, checked against a numpy simulation of the same construction.
- **D-11:** References: independent numpy implementations in `validation/reference/python` (split/asymmetric/adaptive quantiles, bootstrap paths) and Rust `crates/anofox-fcst-core/tests/conformal_reference.rs` with the Monte Carlo coverage tests. A new NON-json-gated `test/sql/ts_conformal_reference.test` holds the SQL assertions; existing ts_conformal.test / ts_conformal_coverage.test are json-gated, and un-gating them is Phase 18. Breaking changes get CHANGELOG before/after rows; docs and the backtest skill are updated; no compatibility flag.
- **D-12:** 3 plans: (1) SQL contract tracer [RUST-04]; (2) split/asymmetric/adaptive core [CONF-02, CONF-03, CONF-04]; (3) bootstrap sign + jackknife+ removal + docs/skill/CHANGELOG [CONF-01, CONF-05].

### Claude's Discretion
- Exact parameter names for the step column and difficulty input (consistent with existing `_by` macro naming), Monte Carlo sample sizes and seeds, and the internal structure of conformal.rs.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `crates/anofox-fcst-core/src/conformal.rs` (2280 lines; strategy enum around :488–:525 with the JackknifePlus aliases) and `bootstrap.rs` (194 lines).
- Phase 12's shared list helper and pairwise-complete validity in the conformal scalars (`src/scalar_functions/conformal.cpp`, `bootstrap.cpp`).
- The validation harness (`validation/reference/python` uv project + `fixture_io.py`; `tests/common/mod.rs::load_fixture()`) and the non-gated `*_reference.test` SQL pattern from Phases 13/14.

### Established Patterns
- SQL macros: `src/macros/ts_macros.cpp` around :1645–:1800 (`ts_conformal_by` :1720, `ts_conformal_calibrate` :1783; the audit's D4–D6 line refs are from before Phase 12 shifted them). They currently build `LIST(... ORDER BY forecast value)` and compute residuals in SQL.
- Breaking statistic changes get CHANGELOG before/after rows and docs/skill updates in the same phase.

### Integration Points
- Skill: `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md`. Docs: the conformal/evaluation pages under `docs/api/` and `docs/guides/03-cross-validation.md`.
- Known crate bug (memory): `ts_cv_forecast_by('AutoEnsemble')` segfaults on crate 0.15.3. Do not route conformal tests through it.

</code_context>

<specifics>
## Specific Ideas

- ROADMAP anchors: horizon step h's interval belongs to forecast step h; two groups with different residual scales get different quantiles; skewed bootstrap tail on the correct side; NULL/unbounded when calibration is insufficient; asymmetric coverage within ±2pp; the adaptive interval is independent of the batch; jackknife+ errors clearly.
- DoD as in Phases 13–15: `cargo fmt --check`, `cargo test --workspace`, `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`, `make test_release` (assertion count rises), snippet harness 0 FAIL for docs.

</specifics>

<deferred>
## Deferred Ideas

- A true jackknife+ (leave-one-out refits), if a refit-capable architecture is ever added.
- Removing the json gate from ts_conformal.test / ts_conformal_coverage.test (Phase 18 / TEST-03).

</deferred>
