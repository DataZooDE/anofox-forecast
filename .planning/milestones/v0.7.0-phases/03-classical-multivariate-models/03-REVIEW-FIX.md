---
phase: 03-classical-multivariate-models
fixed_at: 2026-08-22T08:50:00Z
review_path: .planning/phases/03-classical-multivariate-models/03-REVIEW.md
iteration: 2
findings_in_scope: 2
fixed: 2
skipped: 0
status: all_fixed
---

# Phase 03: Code Review Fix Report (Iteration 2)

**Fixed at:** 2026-08-22T08:50:00Z
**Source review:** `.planning/phases/03-classical-multivariate-models/03-REVIEW.md`
**Iteration:** 2

**Summary:**
- Findings in scope: 2 (1 CR-* + 1 WR-*)
- Fixed: 2
- Skipped: 0

**Build verification:** Both fixes verified by `cargo check`, full test suite
(221 core + 38 FFI tests), a full extension rebuild (Rust + C++), and end-to-end
execution of `examples/forecasting/classical_forecasting_examples.sql`. SQL spot-check
confirmed GARCH and Kalman emit `lower_non_null=0, upper_non_null=0` on all paths;
Naive emits `lower_non_null=5, upper_non_null=5` (intervals intact for other models).
Verification ran in the main checkout (`workflow.use_worktrees=false`).

---

## Fixed Issues

### CR-01: `forecast_with_exog` missing GARCH/Kalman interval gate

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
**Commit:** `04b32da`
**Applied fix:** Added a `match options.model` guard at line 930 in `forecast_with_exog()`
that returns `(vec![], vec![])` for `ModelType::GARCH | ModelType::Kalman`, identical to the
guard already present in `forecast()` (added in iteration 1). All other model types continue
through `calculate_confidence_intervals` as before. This closes the exog path that could have
delivered spurious historical-volatility-based bounds when GARCH or Kalman reached the else
branch (`forecast_with_model`) inside `forecast_with_exog`.

**Verification (main checkout):**
- Tier 1: Re-read confirmed guard text present and surrounding code intact.
- Tier 2: `cargo check -p anofox-fcst-core` passed (0 errors, 0 warnings).
- Tier 2: `cargo test -p anofox-fcst-core` passed (221 unit + 12 doc-tests, 0 failures).
- Tier 2: `cargo test -p anofox-fcst-ffi` passed (38 tests, 0 failures).
- Build: `make rust` + `cmake --build build/release` succeeded end-to-end.
- SQL example: `classical_forecasting_examples.sql` ran cleanly with correct output.
- SQL spot-check: GARCH `lower_non_null=0`, `upper_non_null=0`; Kalman `lower_non_null=0`,
  `upper_non_null=0`; Naive `lower_non_null=5`, `upper_non_null=5`.

---

### WR-01: `list_models` doc comment count stale ("34 models" → "35 models")

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
**Commit:** `9e44360`
**Applied fix:** Updated the `///` doc comment on `list_models()` from
`"34 models matching C++ extension"` to `"35 models matching C++ extension"`. The body
already contained 35 entries (confirmed by `awk` count before editing). No code logic changed.

**Verification (main checkout):**
- Tier 1: Re-read confirmed doc comment updated to 35.
- Tier 2: `cargo check -p anofox-fcst-core` passed (0 errors).
- Tier 2: `cargo test -p anofox-fcst-core -- test_all_model_names_match_enum` passed.

---

_Fixed: 2026-08-22T08:50:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
