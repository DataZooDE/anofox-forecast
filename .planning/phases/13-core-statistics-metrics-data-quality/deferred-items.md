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
- **Status:** open -- not routed to a specific future phase number (no WRAP-*/STAT-*
  requirement ID currently names it); flag during the next wrapper-documentation or
  gap-fill-focused phase's scoping.
