# Deferred Items

Out-of-scope discoveries logged during plan execution (per executor deviation rules'
scope boundary). Not fixed here.

## Plan 13-02

- **Pre-existing `cargo clippy --all-targets -- -D warnings` failure, unrelated to this
  plan's files.** `crates/anofox-fcst-core/src/decomposition.rs:70` and
  `crates/anofox-fcst-core/src/features.rs:53` both carry
  `#[allow(clippy::manual_is_multiple_of)]`, which the locally installed clippy
  (both the `stable` default, 0.1.99, and the pinned `1.89.0` toolchain, 0.1.89) rejects
  as `unknown lint` (`-D unknown-lints` escalates it to a hard error). Separately,
  `crates/anofox-fcst-core/src/periods.rs:851` and `:874` trip
  `clippy::needless_range_loop`. `git diff HEAD -- <those three files>` confirms none of
  them were touched by this plan. This blocks a clean whole-workspace
  `cargo clippy --all-targets --all-features -- -D warnings` run locally (Task 3's DoD
  step) regardless of toolchain pin; `stl.rs`/`supsmu.rs`/`stats.rs` (the files this plan
  touches) are themselves clippy-clean — verified by running the same command and
  confirming zero findings reference those three files.

## Plan 13-03

- **`ParseFrequencyWithType`'s numeric multiplier is discarded for calendar
  (Monthly/Quarterly/Yearly) frequency strings outside the forecast-date path.**
  `src/table_functions/ts_fill_gaps_native.cpp::ParseFrequencyWithType` parses `'3mo'`
  as `{seconds: 3, type: MONTHLY}`, but `_ts_fill_gaps_native`/`_ts_fill_forward_native`,
  `ts_stats.cpp::ParseFrequencyForStats` (introduced 13-01), and this plan's
  `ts_data_quality.cpp::ParseFrequencyForDataQuality` all map `FrequencyType::MONTHLY`
  to a fixed `CalendarStep::Months(1)` (or `Months(3)`/`Months(12)` for
  Quarterly/Yearly) regardless of the parsed count -- so `'3mo'` behaves identically to
  `'1mo'` (steps by 1 month, not 3) on every gap-fill/stats/data-quality surface.
  Discovered while writing `test/sql/ts_fill_gaps_month_end.test`'s quarter-end case
  (`'3mo'` filled every month instead of every quarter; switched the test to the
  already-correct `'1q'` string instead). **Not this plan's bug**: it predates 13-01
  (`ts_fill_gaps_native.cpp` is pre-Phase-12) and is reproduced identically by
  `ts_stats.cpp`'s already-shipped `ParseFrequencyForStats`, which this plan's
  `ParseFrequencyForDataQuality` deliberately mirrors for consistency (D-04/D-05
  parity) rather than silently fixing a different bug along the way.
  `src/include/ts_future_dates.hpp` (the `ts_forecast_by`/`ts_forecast_agg` D-12
  calendar path) DOES multiply by the count (`step_months = freq_seconds * {1,3,12}`),
  so `'3mo'` works correctly there -- the inconsistency is specifically between the
  forecast-date path and every other calendar-frequency consumer.
- **Suggested fix:** a future wrapper-surface phase should make
  `ParseFrequencyForStats`/`ParseFrequencyForDataQuality`/`_ts_fill_gaps_native`/
  `_ts_fill_forward_native` multiply by the parsed count the same way the forecast
  path's `MapFrequencyToStep` does, and add SQL regression tests for
  `'2mo'`/`'2q'`/`'2y'` on every affected surface.
- **Status:** RESOLVED (code review fix pass, 13-REVIEW CR-01, iteration 1). Threaded
  an explicit `period_count` parameter end-to-end -- Rust core (`gaps::fill_gaps`,
  `gaps::fill_forward`, `gaps::resolve_gap_step`, `gaps::step_count`,
  `stats::compute_ts_stats_with_dates_and_type`, `quality::compute_data_quality`),
  the FFI boundary (`anofox_ts_fill_gaps`, `anofox_ts_fill_forward_dates`,
  `anofox_ts_stats_with_dates_and_type`, `anofox_ts_data_quality`; header
  regenerated), and the C++ callers (`_ts_fill_gaps_native`, `_ts_fill_forward_native`,
  `ts_stats.cpp`, `ts_data_quality.cpp`, plus the `_agg` variants passing a literal
  `1`) -- so `'2mo'`/`'3q'`/`'5y'` now step/gap-count by the declared multiplier
  instead of silently behaving like `'1mo'`/`'1q'`/`'1y'` on every affected surface.
  Also consolidated the duplicated `ParseFrequencyForStats`/
  `ParseFrequencyForDataQuality` into one shared
  `ParseFrequencyToMicrosAndType` helper in `ts_fill_gaps_native.hpp`/`.cpp` (WR-02).
  Regression coverage: Rust unit tests in `gaps.rs`/`quality.rs`, integration tests in
  `stats_reference.rs`/`quality_gaps_reference.rs`, and SQL assertions in
  `test/sql/ts_stats.test` (`'2mo'`) and `test/sql/ts_data_quality_reference.test`
  (`'2mo'`/`'3q'`/`'5y'`). `cargo test --workspace` and `make test_release` both green.

## Plan 13-04

- **D-08 scalar/table signature conflict, resolved (recorded for the user).** D-08 writes
  the new MASE signature as `ts_mase(actual, forecast, insample, seasonal_period := 1)`
  AND requires the old `(actual, forecast, baseline)` 3-list form to error. Both forms
  are three DOUBLE lists, so a defaulted `seasonal_period` on the scalar would silently
  reinterpret an old call's `baseline` array as `insample` -- exactly the
  silent-wrong-number failure this milestone exists to remove. **Resolution:** the
  scalar `ts_mase` is a C++ overload set --
  `ts_mase(LIST, LIST, LIST, BIGINT)` computes Hyndman-Koehler MASE and the 3-list
  overload raises a bind-time error naming `ts_rmae`; `seasonal_period` is therefore a
  REQUIRED 4th positional argument on the scalar (DuckDB scalar functions do not accept
  `:=` named arguments at all, so there was never a scalar-level default to begin with).
  The `ts_mase_by` TABLE MACRO CAN keep the default
  (`ts_mase_by(..., seasonal_period := 1)`), because DuckDB resolves a macro's
  positionally-passed-into-a-default-slot argument by TYPE: an old 5-positional call's
  `baseline_col` is a VARCHAR string literal, and the new `seasonal_period` slot expects
  an integer -- so `_ts_mase_native`'s bind detects the VARCHAR type and errors naming
  `ts_rmae_by` instead of silently treating a column-name string as a seasonal period.
  Verified directly against DuckDB 1.5.6 at planning and confirmed again by this plan's
  own `statement error` SQL assertions (`test/sql/ts_metrics_reference.test`,
  `test/sql/ts_metrics.test`).
- **13-02's tsfeatures-vs-periodic STL note (carried forward for visibility).** D-02 named
  `feasts`'s `s.window = "periodic"` STL variant as a possible reference; 13-02 used
  `tsfeatures::stl_features` (built on `forecast::mstl` -> `stats::stl`, `s.window = 11`)
  instead, because `feasts` is not installed in this repo's R environment
  (`validation/reference/README.md` section 5). This means the ROADMAP SC2 audit value
  ("random walk seasonality_strength about 0.05") was the `s.window = "periodic"`
  variant; this plan's shipped value under `s.window = 11` is ~0.0865 instead. Not a bug
  -- both are legitimate STL parameterizations -- but flagged here (as 13-02's own
  SUMMARY already did) in case a future phase installs `feasts` and wants to reconcile
  the two conventions explicitly.
- **The other `_by` metric table functions still filter missing values in C++, not
  Rust.** `_ts_mase_native` (this plan) now passes validity bitmasks straight to Rust
  for both its evaluation pair and its in-sample series, with zero C++-side
  `std::isnan` filtering. `_ts_metrics_native` (MAE/MSE/RMSE/MAPE/SMAPE/R2/BIAS, line
  ~468), `_ts_rmae_native` (~1173), `_ts_coverage_native` (~1468), and
  `_ts_quantile_loss_native` (~1756) in `src/table_functions/ts_metrics_native.cpp` all
  still drop a row when `std::isnan()` is true on any of its NaN-sentinel-encoded
  columns, pre-filtering before ever calling the FFI validity-bitmask path that Rust's
  `missing.rs` owns. Confirmed still true as of this plan (same four call sites 13-03
  already flagged as an open RUST-10 candidate are unchanged by this plan's work).
  **Status:** open, routed to a future wrapper-surface phase (RUST-10 candidate,
  Phase 18) per the existing Phase 18 note in the project roadmap -- not re-scoped here
  since this plan only owned the MASE/MAPE policy change (D-07/D-08), not a
  table-function-wide missing-data-policy migration.
