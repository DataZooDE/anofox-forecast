# Deferred Items — Phase 12 (statistics-marshalling-moves-to-rust)

Out-of-scope discoveries logged per the executor's scope-boundary rule
("only auto-fix issues DIRECTLY caused by the current task's changes"), verified
against the code before being recorded here.

## (a) Several aggregate functions still drop NULL-valued rows instead of a trim/error/pairwise policy

- **Discovered during:** Plan 12-08 Task 3, phase-wide sweep.
- **Symptom:** `src/aggregate_functions/ts_detect_periods_agg.cpp`,
  `ts_changepoints_agg.cpp`, `ts_classify_seasonality_agg.cpp`, `ts_stats_agg.cpp`, and
  `ts_data_quality_agg.cpp` each `continue` past a NULL-valued input row while
  accumulating state — a splice, the same class of bug Phase 12 D-03/D-11 fixed for
  `ts_features_agg`/`ts_features`/single-series diagnostics, but these five aggregates
  were never brought onto the shared Rust NULL policy.
- **Verified:** `grep -n "continue" src/aggregate_functions/{ts_detect_periods_agg,
  ts_changepoints_agg,ts_classify_seasonality_agg,ts_stats_agg,ts_data_quality_agg}.cpp`
  each show multiple NULL-row-skip sites.
- **Why deferred:** Phase 12's requirements (RUST-01..09) enumerate specific
  functions/surfaces; these five aggregates are not among them and are architecturally
  identical work to a future detection/stats phase, not this plan's hierarchy/backtest
  closure scope.
- **Suggested fix:** `ts_detect_periods_agg`, `ts_changepoints_agg`,
  `ts_classify_seasonality_agg` → Phase 15 (detection); `ts_stats_agg`,
  `ts_data_quality_agg` → Phase 13 (wrapper/stats). Apply the same
  trim-edges/error-on-interior-NULL/all-NULL-returns-NULL Rust policy already
  established for `ts_features_agg`.
- **Status:** acknowledged

## (b) Native detection table functions still impute NULL leaf values as 0

- **Discovered during:** Plan 12-08 Task 1 (fixing the identical bug in
  `ts_aggregate_hierarchy`), confirmed present elsewhere during Task 3's sweep.
- **Symptom:** `src/table_functions/ts_changepoints.cpp:404` and
  `src/table_functions/ts_mstl_decomposition_native.cpp:215` both contain
  `row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();` — the exact
  NULL→0 imputation pattern D-08 just removed from `ts_aggregate_hierarchy.cpp`.
- **Verified:** `grep -n "IsNull() ? 0.0" src/table_functions/ts_changepoints.cpp
  src/table_functions/ts_mstl_decomposition_native.cpp` both match.
- **Why deferred:** These are native detection/decomposition table functions, not
  hierarchy aggregation or backtest (this plan's two remaining ROADMAP items); fixing
  them is detection-surface NULL-policy work.
- **Suggested fix:** Apply the same validity-carrying fix as this plan's
  `ts_aggregate_hierarchy` change — carry a `valid` flag per row and decide the correct
  per-function policy (trim edges / error on interior NULL, matching the Rust
  single-series gate already used by the scalar/aggregate paths on the same data).
- **Status:** acknowledged

## (c) Correct-but-private list-marshalling helpers not yet consolidated onto `ts_list_marshal.hpp`

- **Discovered during:** Plan 12-08 Task 3, cross-checking D-01's shared-helper intent
  against every list-reading call site.
- **Symptom:** `src/table_functions/ts_stats.cpp`, `src/table_functions/ts_forecast.cpp`,
  `src/scalar_functions/ts_forecast_scalar.cpp`, and
  `src/scalar_functions/ts_forecast_inspect_scalar.cpp` each still read list arguments
  via their own private, independently-correct helper rather than the shared
  `ListDoubleReader`/`ScalarArgReader` in `src/include/ts_list_marshal.hpp`. These are
  not bugs (each is believed per-row-correct on its own), but they are duplicate
  implementations of the policy D-01 consolidated everywhere else in this phase, and any
  other list-taking function outside Phase 12's touched files that still reads
  parameters by a bare `FlatVector` index (rather than `ToUnifiedFormat`) is the same
  latent per-row-correctness risk D-01/RUST-01 fixed elsewhere.
- **Why deferred:** RUST-10's grep-based "no stats in C++ / no raw FlatVector list
  reads" audit is explicitly assigned to Phase 18, not this phase.
- **Suggested fix:** Phase 18 RUST-10 audit: grep for `FlatVector::GetData` list/array
  reads outside `ts_list_marshal.hpp`'s own call sites, consolidate the four files above
  (and any others the grep finds) onto the shared reader.
- **Status:** acknowledged

## (d) `is_constant`'s absolute `f64::EPSILON` tolerance is not scale-aware

- **Discovered during:** Plan 12-08 Task 3, reviewing `ts_drop_constant_by`'s Rust
  dispatch (12-07) for edge cases while writing this sweep.
- **Symptom:** `crates/anofox-fcst-core/src/filter.rs::is_constant` compares
  `(v - first).abs() < f64::EPSILON` — an absolute tolerance (~2.22e-16). A series whose
  values differ only in the 15th+ significant digit due to floating-point
  accumulation error at a LARGE scale (e.g. values around 1e10) will be classified as
  non-constant even though it is constant to every practical purpose, since
  `f64::EPSILON` at that magnitude is far smaller than the representable precision gap
  between adjacent floats.
- **Verified:** `grep -n "EPSILON\|is_constant" crates/anofox-fcst-core/src/filter.rs`
  confirms the absolute-tolerance comparison at line 47 (as of this plan).
- **Why deferred:** Not part of D-09's scope (D-09 is "use the Rust tolerance", not "pick
  a new tolerance"); correcting the tolerance algorithm is a detection-quality decision,
  not a marshalling-move.
- **Suggested fix:** Phase 15 DET-07 — switch to a relative tolerance (e.g.
  `(v - first).abs() <= f64::EPSILON * first.abs().max(1.0)` or similar scale-aware rule)
  and re-bless any test fixture that relies on the current absolute behaviour.
- **Status:** acknowledged

## (e) `detect_frequency`'s microsecond return unit is undocumented at the public API boundary

- **Discovered during:** Plan 12-08 Task 3, reviewing 12-05's tie-breaking fix
  (delivered in this phase) for any remaining doc gap.
- **Symptom:** `detect_frequency` (in `gaps.rs`) returns its detected step in
  microseconds; the tie-breaking rule (smaller interval wins on a count tie) was fixed
  in Plan 12-05 of this phase, but the function's doc comment and the SQL-facing docs
  that describe frequency detection do not call out the microsecond unit explicitly,
  which is easy to get wrong when wiring a new caller.
- **Why deferred:** This phase's D-12 scope was the tie-breaking determinism fix
  itself (delivered); documenting the unit convention more thoroughly is wrapper-surface
  documentation work assigned to Phase 13.
- **Suggested fix:** Phase 13 WRAP-03 — add an explicit "(microseconds)" unit note to
  `detect_frequency`'s doc comment and any SQL-facing doc/skill page that surfaces its
  result, alongside WRAP-03's other wrapper-documentation work.
- **Status:** acknowledged
  already delivered (Plan 12-05, this phase).

## (f) Conformal NaN-residual handling has no documented finite-sample contract

- **Discovered during:** Plan 12-08 Task 3, reviewing Plan 12-04's conformal/bootstrap
  NULL-policy work for any remaining contract gap.
- **Symptom:** Plan 12-04 (this phase) made conformal/bootstrap NULL handling
  position-preserving and pairwise-complete (`missing::drop_missing`,
  `missing::complete_cases`), but there is no documented guarantee of what happens when
  dropping NaN/NULL residuals leaves too few residuals for a statistically meaningful
  quantile (e.g. a 95% interval from 3 residuals) — the functions still compute
  *something*, but its finite-sample validity at very small residual counts is
  unspecified.
- **Why deferred:** This is a statistical-contract question (what minimum residual
  count is required for a calibrated interval), not a marshalling defect; Phase 12's
  scope here was fixing the splice-shift bug, which Plan 12-04 completed.
- **Suggested fix:** Phase 16 CONF-03 — define and document a minimum-residual-count
  contract (error vs. a documented reduced-confidence NULL) for every conformal/
  bootstrap scalar, alongside CONF-03's other conformal-macro rework.
- **Status:** acknowledged
