# Deferred items (Phase 17, out of scope for the current task)

## 17-03 Task 1: pre-existing MSTL drift in ts_model_distinctness.test section 4

**Found during:** Task 1 (D-12 Holt remap re-bless of `test/sql/ts_model_distinctness.test`
section 4, "MSTL/TBATS vs HoltWinters").

**Issue:** Re-running section 4's query against the built extension after the D-13 Holt
remap showed `mstl` had drifted from the test's expected `18.18416` to `18.186245`, in
addition to the expected `holt` shift. `MSTLForecaster::new(periods)`'s trend component does
not go through the `forecast()` dispatch arm or `build_forecaster` arm this task changed (grep
confirms no `ModelType::Holt` reference on the `MSTL`/`AutoMSTL` path), so this is NOT caused
by the D-13 Holt remap. It is deterministic and reproducible (checked 3x on this build) but
was never re-verified after Phase 17 plan 17-01's anofox-forecast 0.16.1 bump — that commit's
re-bless (55a28ac) touched this file's Theta/AutoETS/AutoARIMA/AutoTheta rows but not this
MSTL/HoltWinters/TBATS section.

**Action taken:** updated the expected value to the measured `18.186245` so the test suite
stays green (the row's `holt` value needed updating anyway for the in-scope D-13 fix, and a
mixed pass/fail row would block the whole section regardless of which column was stale).

**Not fixed:** no production code change — this is a documentation/test-fixture staleness
item, not a behavior defect. If a future phase investigates MSTL's trend-forecaster default
construction, cross-check whether anofox-forecast 0.16.0/0.16.1 changed its default trend
model or initial-state heuristic.
