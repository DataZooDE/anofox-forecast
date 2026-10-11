//! tsfresh/numpy 117-feature parity harness (Phase 14, D-01/D-12/FEAT-05).
//!
//! Driven entirely by `validation/reference/fixtures/tsfresh_features/mapping.json`
//! (the parity contract) and the nine per-series fixtures it maps onto.
//!
//! - `mapping_covers_list_features_exactly`: schema/coverage guard -- the
//!   mapping's 117 names equal `list_features()`'s set exactly, no
//!   duplicates, every row internally well-formed, and (FEAT-05, the
//!   117/117 gate) every single row is `status: "match"` -- no
//!   `pending:<owner>` row can exist any more.
//! - `tsfresh_parity_<series>` (one per fixture series): asserts every row's
//!   feature against `extract_features()`'s output within the mapping's
//!   tolerance (or the row's own `tolerance` override, each carrying a
//!   reason per D-10), honouring any `deviation` entry. Also asserts the
//!   fixture's own `expected` object has exactly the 117 mapping names, and
//!   that exactly 117 assertions were made (every row, no silent skips).
//!
//! NaN <-> NaN and equal infinities count as a match (D-11); NaN against a
//! number (either direction) fails -- enforced by `common::assert_close`.

mod common;

use anofox_fcst_core::{extract_features, list_features};
use common::{f64_vec, load_fixture, num, tolerance};
use serde_json::Value;
use std::collections::HashSet;

const SERIES_NAMES: [&str; 9] = [
    "white_noise_n200",
    "random_walk_n200",
    "seasonal_ar12_n144",
    "trend_season_n120",
    "intermittent_n100",
    "short_n20",
    "constant_n30",
    "all_zero_n50",
    "tiny_n8",
];

const VALID_STATUSES: [&str; 1] = ["match"];

fn load_mapping_rows() -> Vec<Value> {
    let mapping = load_fixture("tsfresh_features/mapping.json");
    mapping["features"]
        .as_array()
        .expect("tsfresh_features/mapping.json: 'features' is not an array")
        .clone()
}

/// Row-level tolerance: the row's own `tolerance` override (abs/rel) if
/// present, else the fixture's own `tolerance.abs`/`tolerance.rel` (D-10 --
/// never hard-code a tolerance in Rust).
fn row_tolerance(fixture: &Value, row: &Value) -> (f64, f64) {
    if let Some(t) = row.get("tolerance") {
        let abs = t
            .get("abs")
            .map(num)
            .unwrap_or_else(|| tolerance(fixture, "abs"));
        let rel = t
            .get("rel")
            .map(num)
            .unwrap_or_else(|| tolerance(fixture, "rel"));
        (abs, rel)
    } else {
        (tolerance(fixture, "abs"), tolerance(fixture, "rel"))
    }
}

/// Resolves the value a row should be asserted against for a given series:
/// the row's `deviation.expected_ours` when this series is named in
/// `deviation.series`, else the fixture's own tsfresh/numpy `expected` value.
fn expected_value_for(row: &Value, series_name: &str, fixture_expected: f64) -> f64 {
    if let Some(deviation) = row.get("deviation") {
        let series_list = deviation["series"]
            .as_array()
            .map(|a| a.iter().filter_map(|v| v.as_str()).collect::<Vec<_>>())
            .unwrap_or_default();
        if series_list.contains(&series_name) {
            let expected_ours = &deviation["expected_ours"];
            return match expected_ours.as_str() {
                Some("NaN") => f64::NAN,
                _ => num(expected_ours),
            };
        }
    }
    fixture_expected
}

fn run_series_parity(series_name: &str) {
    let rows = load_mapping_rows();
    assert!(!rows.is_empty(), "mapping.json: 'features' is empty");

    let fixture = load_fixture(&format!("tsfresh_features/{series_name}.json"));
    let expected = fixture["expected"]
        .as_object()
        .unwrap_or_else(|| panic!("{series_name}: fixture has no 'expected' object"));
    assert_eq!(
        expected.len(),
        rows.len(),
        "{series_name}: fixture 'expected' has {} keys, mapping.json has {} rows -- regenerate the fixture",
        expected.len(),
        rows.len()
    );
    let mapping_names: HashSet<&str> = rows.iter().map(|r| r["name"].as_str().unwrap()).collect();
    for key in expected.keys() {
        assert!(
            mapping_names.contains(key.as_str()),
            "{series_name}: fixture 'expected' has key {key:?} not present in mapping.json"
        );
    }

    let values = f64_vec(&fixture["series"]["values"]);
    let got = extract_features(&values)
        .unwrap_or_else(|e| panic!("{series_name}: extract_features failed: {e:?}"));

    let mut asserted = 0usize;
    for row in &rows {
        let name = row["name"]
            .as_str()
            .unwrap_or_else(|| panic!("{series_name}: mapping row missing string 'name'"));
        let status = row["status"]
            .as_str()
            .unwrap_or_else(|| panic!("{series_name}.{name}: mapping row missing string 'status'"));
        assert_eq!(
            status, "match",
            "{series_name}.{name}: status is {status:?}, but the 117/117 gate (FEAT-05) requires every row to be \"match\""
        );

        let fixture_expected = expected
            .get(name)
            .map(num)
            .unwrap_or_else(|| panic!("{series_name}: fixture 'expected' missing key {name:?}"));
        let got_value = got.get(name).copied().unwrap_or(f64::NAN);

        let want = expected_value_for(row, series_name, fixture_expected);
        let (abs_tol, rel_tol) = row_tolerance(&fixture, row);
        common::assert_close(
            got_value,
            want,
            abs_tol,
            rel_tol,
            &format!("{series_name}.{name}"),
        );
        asserted += 1;
    }

    assert_eq!(
        asserted, 117,
        "{series_name}: expected exactly 117 feature assertions (the 117/117 gate), got {asserted}"
    );
}

#[test]
fn tsfresh_parity_white_noise_n200() {
    run_series_parity("white_noise_n200");
}

#[test]
fn tsfresh_parity_random_walk_n200() {
    run_series_parity("random_walk_n200");
}

#[test]
fn tsfresh_parity_seasonal_ar12_n144() {
    run_series_parity("seasonal_ar12_n144");
}

#[test]
fn tsfresh_parity_trend_season_n120() {
    run_series_parity("trend_season_n120");
}

#[test]
fn tsfresh_parity_intermittent_n100() {
    run_series_parity("intermittent_n100");
}

#[test]
fn tsfresh_parity_short_n20() {
    run_series_parity("short_n20");
}

#[test]
fn tsfresh_parity_constant_n30() {
    run_series_parity("constant_n30");
}

#[test]
fn tsfresh_parity_all_zero_n50() {
    run_series_parity("all_zero_n50");
}

#[test]
fn tsfresh_parity_tiny_n8() {
    run_series_parity("tiny_n8");
}

/// Sanity check that every series name this harness loads actually has a
/// fixture file on disk (keeps the SERIES_NAMES list and the fixtures
/// directory from silently drifting apart).
#[test]
fn all_nine_series_fixtures_present() {
    for name in SERIES_NAMES {
        let _ = load_fixture(&format!("tsfresh_features/{name}.json"));
    }
}

/// ROADMAP Phase 14 success criterion 4's literal anchor (Task 1, D-08): on
/// the audit `short_n20` vector, `sample_entropy` must equal `ln(3) =
/// 1.0986122886681098` (previously `0.9808292530117262` under the old
/// window-count-off-by-one/triangular-sum bug), pinned independently of the
/// fixture JSON the same way `stats_reference.rs`'s `r_entropy` test pins
/// its own literal values.
#[test]
fn sample_entropy_roadmap_anchor() {
    let fixture = load_fixture("tsfresh_features/short_n20.json");
    let values = f64_vec(&fixture["series"]["values"]);
    let got = extract_features(&values).unwrap();

    let want = 1.0986122886681098_f64; // ln(3)
    let got_value = *got.get("sample_entropy").unwrap();
    common::assert_close(
        got_value,
        want,
        1e-12,
        0.0,
        "short_n20.sample_entropy_roadmap_anchor",
    );
}

/// D-01 completeness gate: the mapping's names equal `list_features()`'s set
/// exactly (117 rows, no duplicates), and every row is internally
/// well-formed (D-10/D-11 preconditions the other tests rely on).
#[test]
fn mapping_covers_list_features_exactly() {
    let rows = load_mapping_rows();

    let mapping_names: Vec<String> = rows
        .iter()
        .map(|r| {
            r["name"]
                .as_str()
                .unwrap_or_else(|| panic!("mapping row missing string 'name': {r:?}"))
                .to_string()
        })
        .collect();

    assert_eq!(
        mapping_names.len(),
        117,
        "mapping.json: expected 117 rows, got {}",
        mapping_names.len()
    );

    let unique_names: HashSet<&String> = mapping_names.iter().collect();
    assert_eq!(
        unique_names.len(),
        mapping_names.len(),
        "mapping.json: duplicate feature names present"
    );

    let list_features_set: HashSet<String> = list_features().into_iter().collect();
    let mapping_set: HashSet<String> = mapping_names.into_iter().collect();
    assert_eq!(
        mapping_set, list_features_set,
        "mapping.json names != list_features() names (symmetric difference indicates drift)"
    );

    for row in &rows {
        let name = row["name"].as_str().unwrap();

        let reference = row["reference"]
            .as_str()
            .unwrap_or_else(|| panic!("{name}: missing string 'reference'"));
        assert!(
            reference == "tsfresh" || reference == "numpy/scipy",
            "{name}: reference must be 'tsfresh' or 'numpy/scipy', got {reference:?}"
        );
        if reference == "tsfresh" {
            assert!(
                row.get("calculator").is_some(),
                "{name}: tsfresh row missing 'calculator'"
            );
            assert!(
                row.get("params").is_some(),
                "{name}: tsfresh row missing 'params'"
            );
        } else {
            assert!(
                row.get("formula").is_some(),
                "{name}: numpy/scipy row missing 'formula'"
            );
        }

        let status = row["status"]
            .as_str()
            .unwrap_or_else(|| panic!("{name}: missing string 'status'"));
        assert!(
            VALID_STATUSES.contains(&status),
            "{name}: status {status:?} is not one of {VALID_STATUSES:?}"
        );

        if let Some(deviation) = row.get("deviation") {
            let series_list = deviation["series"]
                .as_array()
                .unwrap_or_else(|| panic!("{name}: 'deviation.series' is not an array"));
            assert!(
                !series_list.is_empty(),
                "{name}: 'deviation.series' is empty"
            );
            let reason = deviation["reason"]
                .as_str()
                .unwrap_or_else(|| panic!("{name}: 'deviation.reason' is missing"));
            assert!(!reason.is_empty(), "{name}: 'deviation.reason' is empty");
        }

        if let Some(t) = row.get("tolerance") {
            let reason = t["reason"]
                .as_str()
                .unwrap_or_else(|| panic!("{name}: 'tolerance.reason' is missing"));
            assert!(!reason.is_empty(), "{name}: 'tolerance.reason' is empty");
        }
    }
}
