//! Reference-fixture assertions for `compute_data_quality` (Phase 13 plan 13-03 task 2:
//! D-05/D-06, STAT-04) and month-end-preserving gap fill (task 3: D-09, WRAP-03).
//!
//! - `quality_scores_fixture_matches_compute_data_quality`: asserts every score
//!   (`structural_score`, `temporal_score`, `magnitude_score`, `behavioral_score`,
//!   `overall_score`), `n_gaps`, `n_gap_runs`, `n_missing`, `is_constant`, `is_short`
//!   against `quality_scores/cases.json` -- an independent numpy/scipy
//!   reimplementation of the documented formulas (D-06).
//! - `behavioral_score_determinism_on_tied_steps`: 50-run loop confirming the tied-step
//!   determinism also holds end-to-end through `compute_data_quality`.
//! - `fill_gaps_fixture_matches_gap_counts_fill_cases` (added task 3): asserts
//!   `gaps::fill_gaps` against every `gap_counts/cases.json` `fill_cases` entry.
//! - `fill_forward_month_end_matches_calendar_rule` (added task 3): a `fill_forward`
//!   month-end case (2024-03-31 -> 2024-06-30).

mod common;

use anofox_fcst_core::compute_data_quality;
use anofox_fcst_core::gaps::{fill_forward, fill_gaps};
use anofox_fcst_core::FrequencyType;
use chrono::NaiveDateTime;
use common::{load_fixture, num, tolerance};

/// Parses an ISO-8601 naive timestamp (as emitted by `pandas.Timestamp.isoformat()`,
/// e.g. `"2024-01-31T00:00:00"`) into microseconds since the Unix epoch.
fn iso_to_micros(s: &str) -> i64 {
    let dt = NaiveDateTime::parse_from_str(s, "%Y-%m-%dT%H:%M:%S")
        .unwrap_or_else(|e| panic!("iso_to_micros({s:?}): parse failed: {e}"));
    dt.and_utc().timestamp_micros()
}

/// Decodes a fixture `values` array entry into `Option<f64>` (`null` -> `None`,
/// `"NaN"` -> `Some(f64::NAN)`, number -> `Some(number)`).
fn decode_value(v: &serde_json::Value) -> Option<f64> {
    match v {
        serde_json::Value::Null => None,
        other => Some(num(other)),
    }
}

#[test]
fn quality_scores_fixture_matches_compute_data_quality() {
    let fx = load_fixture("quality_scores/cases.json");
    let score_tol = tolerance(&fx, "score_abs");

    let cases = fx["cases"]
        .as_object()
        .expect("quality_scores_fixture_matches_compute_data_quality: 'cases' is not an object");
    assert!(!cases.is_empty(), "'cases' is empty");

    for (name, case) in cases {
        let values: Vec<Option<f64>> = case["values"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'values' is not an array"))
            .iter()
            .map(decode_value)
            .collect();
        let n_short = num(&case["n_short"]) as usize;

        let dates: Option<Vec<i64>> = case.get("dates").and_then(|d| d.as_array()).map(|arr| {
            arr.iter()
                .map(|v| iso_to_micros(v.as_str().expect("date entry is not a string")))
                .collect()
        });

        let (frequency_micros, frequency_type) = match case["frequency"].as_str() {
            Some("1d") => (86_400_000_000_i64, FrequencyType::Fixed),
            None => (0_i64, FrequencyType::Fixed),
            other => panic!("{name}: unhandled frequency {other:?}"),
        };

        let quality = compute_data_quality(
            &values,
            dates.as_deref(),
            frequency_micros,
            frequency_type,
            n_short,
            1,
        )
        .unwrap_or_else(|e| panic!("{name}: compute_data_quality failed: {e:?}"));

        common::assert_close(
            quality.structural_score,
            num(&case["structural_score"]),
            score_tol,
            0.0,
            &format!("{name}.structural_score"),
        );
        common::assert_close(
            quality.temporal_score,
            num(&case["temporal_score"]),
            score_tol,
            0.0,
            &format!("{name}.temporal_score"),
        );
        common::assert_close(
            quality.magnitude_score,
            num(&case["magnitude_score"]),
            score_tol,
            0.0,
            &format!("{name}.magnitude_score"),
        );
        common::assert_close(
            quality.behavioral_score,
            num(&case["behavioral_score"]),
            score_tol,
            0.0,
            &format!("{name}.behavioral_score"),
        );
        common::assert_close(
            quality.overall_score,
            num(&case["overall_score"]),
            score_tol,
            0.0,
            &format!("{name}.overall_score"),
        );
        assert_eq!(
            quality.n_gaps,
            num(&case["n_gaps"]) as usize,
            "{name}.n_gaps"
        );
        assert_eq!(
            quality.n_gap_runs,
            num(&case["n_gap_runs"]) as usize,
            "{name}.n_gap_runs"
        );
        assert_eq!(
            quality.n_missing,
            num(&case["n_missing"]) as usize,
            "{name}.n_missing"
        );
        assert_eq!(
            quality.is_constant,
            case["is_constant"].as_bool().unwrap(),
            "{name}.is_constant"
        );
        assert_eq!(
            quality.is_short,
            case["is_short"].as_bool().unwrap(),
            "{name}.is_short"
        );
    }
}

#[test]
fn behavioral_score_determinism_on_tied_steps() {
    // diffs: 1, 2 (tie) -> smaller (1 day) wins, every time (50-run loop).
    const ONE_DAY_MICROS: i64 = 86_400_000_000;
    let dates = vec![0, ONE_DAY_MICROS, 3 * ONE_DAY_MICROS];
    let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];

    let mut results = Vec::new();
    for _ in 0..50 {
        let q = compute_data_quality(&values, Some(&dates), 0, FrequencyType::Fixed, 1, 1).unwrap();
        results.push((q.n_gaps, q.n_gap_runs));
    }
    assert!(
        results.iter().all(|r| *r == results[0]),
        "expected identical results across 50 runs, got {results:?}"
    );
    assert_eq!(results[0], (1, 1));
}

/// Builds the step the `gap_counts` fixture's `fill_cases`/`cases` `step` object
/// describes, as `(frequency_micros, frequency_type)` for `gaps::fill_gaps`.
fn fill_step_from_name(name: &str) -> (i64, FrequencyType) {
    match name {
        "month_end" | "month_start" | "mid_month_15" => (0, FrequencyType::Monthly),
        "quarter_end" => (0, FrequencyType::Quarterly),
        "year_end" => (0, FrequencyType::Yearly),
        other => panic!("fill_step_from_name: unhandled fill_cases entry {other:?}"),
    }
}

#[test]
fn fill_gaps_fixture_matches_gap_counts_fill_cases() {
    let fx = load_fixture("gap_counts/cases.json");
    let fill_cases = fx["fill_cases"]
        .as_object()
        .expect("fill_gaps_fixture_matches_gap_counts_fill_cases: 'fill_cases' is not an object");
    assert!(!fill_cases.is_empty(), "'fill_cases' is empty");

    for (name, case) in fill_cases {
        let observed: Vec<i64> = case["observed"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'observed' is not an array"))
            .iter()
            .map(|v| iso_to_micros(v.as_str().expect("observed entry is not a string")))
            .collect();
        let expected_filled: Vec<i64> = case["expected_filled"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'expected_filled' is not an array"))
            .iter()
            .map(|v| iso_to_micros(v.as_str().expect("expected_filled entry is not a string")))
            .collect();

        let values: Vec<Option<f64>> = observed.iter().map(|_| Some(1.0)).collect();
        let (frequency_micros, frequency_type) = fill_step_from_name(name);

        let (filled_dates, _) = fill_gaps(&observed, &values, frequency_micros, frequency_type, 1)
            .unwrap_or_else(|e| panic!("{name}: fill_gaps failed: {e:?}"));

        assert_eq!(filled_dates, expected_filled, "{name}: filled dates");
    }
}

#[test]
fn fill_forward_month_end_matches_calendar_rule() {
    // 2024-03-31 observed; fill_forward to 2024-06-30 should land on 04-30, 05-31, 06-30
    // (month-end preserved throughout), not the 1st-of-month snapping the pre-D-09
    // implementation produced.
    fn ymd_micros(y: i32, m: u32, d: u32) -> i64 {
        use chrono::NaiveDate;
        let date = NaiveDate::from_ymd_opt(y, m, d).unwrap();
        date.and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp_micros()
    }

    let dates = vec![ymd_micros(2024, 3, 31)];
    let values: Vec<Option<f64>> = vec![Some(1.0)];
    let target = ymd_micros(2024, 6, 30);

    let (filled_dates, filled_values) =
        fill_forward(&dates, &values, target, 0, FrequencyType::Monthly, 1).unwrap();

    assert_eq!(
        filled_dates,
        vec![
            ymd_micros(2024, 3, 31),
            ymd_micros(2024, 4, 30),
            ymd_micros(2024, 5, 31),
            ymd_micros(2024, 6, 30),
        ]
    );
    assert_eq!(filled_values, vec![Some(1.0), None, None, None]);
}

/// CR-01 regression (13-03 deferred item): `fill_gaps`/`compute_data_quality` must
/// honor the declared calendar multiplier (`period_count`) for Monthly/Quarterly/
/// Yearly frequencies, not hard-code the single-unit step (`'2mo'` previously
/// behaved identically to `'1mo'`).
#[test]
fn fill_gaps_and_data_quality_honor_calendar_period_count() {
    fn ymd_micros(y: i32, m: u32, d: u32) -> i64 {
        use chrono::NaiveDate;
        NaiveDate::from_ymd_opt(y, m, d)
            .unwrap()
            .and_hms_opt(0, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp_micros()
    }

    // Correctly-spaced bi-monthly series: Jan, Mar, May, Jul 2023.
    let dates = vec![
        ymd_micros(2023, 1, 1),
        ymd_micros(2023, 3, 1),
        ymd_micros(2023, 5, 1),
        ymd_micros(2023, 7, 1),
    ];
    let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

    // fill_gaps with period_count=2 ('2mo') must insert NOTHING.
    let (filled_dates, filled_values) =
        fill_gaps(&dates, &values, 0, FrequencyType::Monthly, 2).unwrap();
    assert_eq!(filled_dates, dates);
    assert_eq!(filled_values, values);

    // compute_data_quality with period_count=2 must report n_gaps=0.
    let quality =
        compute_data_quality(&values, Some(&dates), 0, FrequencyType::Monthly, 1, 2).unwrap();
    assert_eq!(quality.n_gaps, 0);
    assert_eq!(quality.n_gap_runs, 0);

    // Without the multiplier (period_count=1, the pre-fix hard-coded behavior), the
    // same series is misclassified as having 3 one-month gaps.
    let (filled_dates_unmultiplied, _) =
        fill_gaps(&dates, &values, 0, FrequencyType::Monthly, 1).unwrap();
    assert_eq!(filled_dates_unmultiplied.len(), 7); // Jan..Jul, every month
}
