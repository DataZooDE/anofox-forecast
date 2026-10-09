//! Calendar-aware date stepping for forecast horizons (D-12).
//!
//! `ts_forecast_agg` / `ts_forecast_by` previously computed forecast dates by
//! adding `k * median_step` microseconds to the last observed timestamp — on
//! monthly data ending in a short month (28/29/30-day) this drifted the
//! forecast dates away from month-end (e.g. a series of month-ends ending
//! `2023-11-30` would forecast `2023-12-30` instead of `2023-12-31`).
//!
//! This module owns the D-12 calendar rule in Rust: [`detect_step`]
//! classifies an observed date series as either a fixed microsecond step or
//! a whole-month step (1/3/12 months), and [`future_dates`] applies the
//! month-end-preserving / day-of-month-clamping rule from the last observed
//! date. C++ only maps a frequency spec to a [`CalendarStep`] and copies the
//! returned dates (see `src/include/ts_future_dates.hpp`).
//!
//! Reusable by Phase 13's planned month-end-aware `fill_gaps` — the same
//! month-end / clamp rule applies there.

use crate::error::{ForecastError, Result};
use crate::gaps::{datetime_to_micros, detect_frequency, micros_to_datetime};
use chrono::{Datelike, NaiveDate};

/// One day in microseconds — the historical default step when fewer than 2
/// distinct observations are available (preserves `ts_forecast_agg`'s
/// pre-existing single-point-series behavior).
pub const ONE_DAY_MICROS: i64 = 86_400_000_000;

/// A calendar step used to compute future dates from a base timestamp.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CalendarStep {
    /// Step by whole calendar months (1 = monthly, 3 = quarterly, 12 = yearly).
    Months(u32),
    /// Step by a fixed number of microseconds (the input's integer unit).
    Fixed(i64),
}

/// Last day-of-month for `(year, month)`, via chrono's calendar (handles leap years).
fn last_day_of_month(year: i32, month: u32) -> Result<u32> {
    let next_first = if month == 12 {
        NaiveDate::from_ymd_opt(
            year.checked_add(1)
                .ok_or_else(|| ForecastError::InvalidInput("year overflow".to_string()))?,
            1,
            1,
        )
    } else {
        NaiveDate::from_ymd_opt(year, month + 1, 1)
    }
    .ok_or_else(|| ForecastError::InvalidInput("invalid year/month in future_dates".to_string()))?;
    let last = next_first
        .pred_opt()
        .ok_or_else(|| ForecastError::InvalidInput("invalid date in future_dates".to_string()))?;
    Ok(last.day())
}

/// Whether `micros` falls on the last day of its calendar month.
fn is_month_end(micros: i64) -> Result<bool> {
    let dt = micros_to_datetime(micros);
    let last_day = last_day_of_month(dt.year(), dt.month())?;
    Ok(dt.day() == last_day)
}

/// Classify an observed date series into a [`CalendarStep`].
///
/// Fewer than 2 distinct observations -> `Fixed(ONE_DAY_MICROS)` (one day;
/// preserves `ts_forecast_agg`'s historical default for a single-point
/// series). Otherwise the mode step (via [`detect_frequency`], which breaks
/// ties deterministically by the smaller interval) is classified: a whole
/// number of days in `28..=31` -> `Months(1)`, `89..=92` -> `Months(3)`,
/// `365..=366` -> `Months(12)`; anything else stays `Fixed(mode)`.
pub fn detect_step(observed: &[i64]) -> Result<CalendarStep> {
    let mut distinct = observed.to_vec();
    distinct.sort_unstable();
    distinct.dedup();
    if distinct.len() < 2 {
        return Ok(CalendarStep::Fixed(ONE_DAY_MICROS));
    }

    let mode = detect_frequency(observed)?;
    if mode > 0 && mode % ONE_DAY_MICROS == 0 {
        let days = mode / ONE_DAY_MICROS;
        if (28..=31).contains(&days) {
            return Ok(CalendarStep::Months(1));
        } else if (89..=92).contains(&days) {
            return Ok(CalendarStep::Months(3));
        } else if (365..=366).contains(&days) {
            return Ok(CalendarStep::Months(12));
        }
    }
    Ok(CalendarStep::Fixed(mode))
}

/// Compute `horizon` future dates (microseconds) stepping forward from
/// `max(observed)` by `step`.
///
/// `Fixed(s)`: `base + k*s` for `k = 1..=horizon` (`s` must be `> 0`).
///
/// `Months(m)`: the target month is `base_month + k*m`, computed fresh from
/// the base each step (never cumulative). If every entry in `observed` falls
/// on the last day of its month, the result is also month-end (clamped to
/// the target month's length); otherwise the result keeps the base's
/// day-of-month, clamped to the target month's length (D-12). The
/// time-of-day of `base` is preserved throughout.
pub fn future_dates(observed: &[i64], horizon: usize, step: CalendarStep) -> Result<Vec<i64>> {
    if observed.is_empty() {
        return Err(ForecastError::InvalidInput(
            "observed must not be empty".to_string(),
        ));
    }
    let base = *observed
        .iter()
        .max()
        .ok_or_else(|| ForecastError::InvalidInput("observed must not be empty".to_string()))?;

    match step {
        CalendarStep::Fixed(s) => {
            if s <= 0 {
                return Err(ForecastError::InvalidInput(
                    "Fixed calendar step must be > 0".to_string(),
                ));
            }
            let mut out = Vec::with_capacity(horizon);
            for k in 1..=horizon as i64 {
                let delta = s
                    .checked_mul(k)
                    .ok_or_else(|| ForecastError::InvalidInput("step * horizon overflow".to_string()))?;
                let date = base
                    .checked_add(delta)
                    .ok_or_else(|| ForecastError::InvalidInput("future date overflow".to_string()))?;
                out.push(date);
            }
            Ok(out)
        }
        CalendarStep::Months(m) => {
            if m == 0 {
                return Err(ForecastError::InvalidInput(
                    "Months calendar step must be > 0".to_string(),
                ));
            }
            let base_dt = micros_to_datetime(base);
            let base_time = base_dt.time();
            let base_day = base_dt.day();

            let mut all_month_end = true;
            for &d in observed {
                if !is_month_end(d)? {
                    all_month_end = false;
                    break;
                }
            }

            let base_total_months = base_dt.year() as i64 * 12 + (base_dt.month() as i64 - 1);

            let mut out = Vec::with_capacity(horizon);
            for k in 1..=horizon as i64 {
                let months_to_add = (m as i64)
                    .checked_mul(k)
                    .ok_or_else(|| ForecastError::InvalidInput("month step * horizon overflow".to_string()))?;
                let total_months = base_total_months
                    .checked_add(months_to_add)
                    .ok_or_else(|| ForecastError::InvalidInput("month arithmetic overflow".to_string()))?;

                let new_year = i32::try_from(total_months.div_euclid(12))
                    .map_err(|_| ForecastError::InvalidInput("year out of range".to_string()))?;
                let new_month = (total_months.rem_euclid(12) + 1) as u32;

                let max_day = last_day_of_month(new_year, new_month)?;
                let new_day = if all_month_end {
                    max_day
                } else {
                    base_day.min(max_day)
                };

                let new_date = NaiveDate::from_ymd_opt(new_year, new_month, new_day).ok_or_else(|| {
                    ForecastError::InvalidInput("invalid date in future_dates".to_string())
                })?;
                let new_dt = new_date.and_time(base_time);
                out.push(datetime_to_micros(new_dt));
            }
            Ok(out)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build a UTC-midnight timestamp (microseconds since epoch) from a
    /// (year, month, day) calendar date.
    fn ymd_micros(year: i32, month: u32, day: u32) -> i64 {
        let date = NaiveDate::from_ymd_opt(year, month, day).unwrap();
        datetime_to_micros(date.and_hms_opt(0, 0, 0).unwrap())
    }

    fn ymd_hms_micros(year: i32, month: u32, day: u32, h: u32, min: u32, s: u32) -> i64 {
        let date = NaiveDate::from_ymd_opt(year, month, day).unwrap();
        datetime_to_micros(date.and_hms_opt(h, min, s).unwrap())
    }

    #[test]
    fn detect_step_month_end_series_is_months_one() {
        let dates: Vec<i64> = (0..11)
            .map(|i| {
                let date = NaiveDate::from_ymd_opt(2023, 1, 1).unwrap() + chrono::Months::new(i);
                // last day of that month
                let next = date + chrono::Months::new(1);
                let last = next.pred_opt().unwrap();
                datetime_to_micros(last.and_hms_opt(0, 0, 0).unwrap())
            })
            .collect();
        assert_eq!(detect_step(&dates).unwrap(), CalendarStep::Months(1));
    }

    #[test]
    fn detect_step_quarter_end_series_is_months_three() {
        let dates = vec![
            ymd_micros(2022, 3, 31),
            ymd_micros(2022, 6, 30),
            ymd_micros(2022, 9, 30),
            ymd_micros(2022, 12, 31),
            ymd_micros(2023, 3, 31),
        ];
        assert_eq!(detect_step(&dates).unwrap(), CalendarStep::Months(3));
    }

    #[test]
    fn detect_step_year_end_series_is_months_twelve() {
        let dates = vec![
            ymd_micros(2019, 12, 31),
            ymd_micros(2020, 12, 31),
            ymd_micros(2021, 12, 31),
            ymd_micros(2022, 12, 31),
            ymd_micros(2023, 12, 31),
        ];
        assert_eq!(detect_step(&dates).unwrap(), CalendarStep::Months(12));
    }

    #[test]
    fn detect_step_daily_series_is_fixed_one_day() {
        let dates = vec![
            ymd_micros(2023, 1, 1),
            ymd_micros(2023, 1, 2),
            ymd_micros(2023, 1, 3),
        ];
        assert_eq!(detect_step(&dates).unwrap(), CalendarStep::Fixed(ONE_DAY_MICROS));
    }

    #[test]
    fn detect_step_single_date_is_fixed_one_day() {
        let dates = vec![ymd_micros(2023, 1, 1)];
        assert_eq!(detect_step(&dates).unwrap(), CalendarStep::Fixed(ONE_DAY_MICROS));
    }

    #[test]
    fn future_dates_month_end_series_stays_month_end_through_february() {
        let dates: Vec<i64> = (0..11)
            .map(|i| {
                let date = NaiveDate::from_ymd_opt(2023, 1, 1).unwrap() + chrono::Months::new(i);
                let next = date + chrono::Months::new(1);
                let last = next.pred_opt().unwrap();
                datetime_to_micros(last.and_hms_opt(0, 0, 0).unwrap())
            })
            .collect();
        let out = future_dates(&dates, 3, CalendarStep::Months(1)).unwrap();
        assert_eq!(
            out,
            vec![
                ymd_micros(2023, 12, 31),
                ymd_micros(2024, 1, 31),
                ymd_micros(2024, 2, 29),
            ]
        );
    }

    #[test]
    fn future_dates_non_month_end_series_clamps_to_last_day() {
        // 2023-01-30, 2023-02-28, 2023-03-30, 2023-04-30 — not all month-ends
        // (Feb 28 IS month-end in a non-leap year, but Jan/Mar/Apr 30 are not)
        let dates = vec![
            ymd_micros(2023, 1, 30),
            ymd_micros(2023, 2, 28),
            ymd_micros(2023, 3, 30),
            ymd_micros(2023, 4, 30),
        ];
        let out = future_dates(&dates, 3, CalendarStep::Months(1)).unwrap();
        assert_eq!(
            out,
            vec![
                ymd_micros(2023, 5, 30),
                ymd_micros(2023, 6, 30),
                ymd_micros(2023, 7, 30),
            ]
        );
    }

    #[test]
    fn future_dates_mid_month_series_keeps_day_of_month() {
        let dates = vec![
            ymd_micros(2023, 1, 15),
            ymd_micros(2023, 2, 15),
            ymd_micros(2023, 3, 15),
            ymd_micros(2023, 4, 15),
            ymd_micros(2023, 5, 15),
            ymd_micros(2023, 6, 15),
        ];
        let out = future_dates(&dates, 2, CalendarStep::Months(1)).unwrap();
        assert_eq!(out, vec![ymd_micros(2023, 7, 15), ymd_micros(2023, 8, 15)]);
    }

    #[test]
    fn future_dates_single_month_end_observation_counts_as_all_month_end() {
        let dates = vec![ymd_micros(2024, 1, 31)];
        let out = future_dates(&dates, 2, CalendarStep::Months(1)).unwrap();
        assert_eq!(out, vec![ymd_micros(2024, 2, 29), ymd_micros(2024, 3, 31)]);
    }

    #[test]
    fn future_dates_quarter_end_series_steps_by_three_months() {
        let dates = vec![
            ymd_micros(2022, 9, 30),
            ymd_micros(2022, 12, 31),
            ymd_micros(2023, 3, 31),
        ];
        let out = future_dates(&dates, 3, CalendarStep::Months(3)).unwrap();
        assert_eq!(
            out,
            vec![
                ymd_micros(2023, 6, 30),
                ymd_micros(2023, 9, 30),
                ymd_micros(2023, 12, 31),
            ]
        );
    }

    #[test]
    fn future_dates_year_end_series_steps_by_twelve_months() {
        let dates = vec![ymd_micros(2022, 12, 31), ymd_micros(2023, 12, 31)];
        let out = future_dates(&dates, 2, CalendarStep::Months(12)).unwrap();
        assert_eq!(out, vec![ymd_micros(2024, 12, 31), ymd_micros(2025, 12, 31)]);
    }

    #[test]
    fn future_dates_preserves_time_of_day() {
        let dates = vec![
            ymd_hms_micros(2023, 1, 31, 13, 0, 0),
            ymd_hms_micros(2022, 12, 31, 13, 0, 0),
        ];
        let out = future_dates(&dates, 1, CalendarStep::Months(1)).unwrap();
        assert_eq!(out, vec![ymd_hms_micros(2023, 2, 28, 13, 0, 0)]);
    }

    #[test]
    fn future_dates_fixed_step_is_arithmetic() {
        let dates = vec![0, ONE_DAY_MICROS];
        let out = future_dates(&dates, 3, CalendarStep::Fixed(ONE_DAY_MICROS)).unwrap();
        assert_eq!(
            out,
            vec![
                ONE_DAY_MICROS * 2,
                ONE_DAY_MICROS * 3,
                ONE_DAY_MICROS * 4,
            ]
        );
    }
}
