//! Reference-fixture harness (TEST-01).
//!
//! Two tests:
//! - `fixture_provenance_contract`: guards every fixture file (present and
//!   future) for the required provenance keys.
//! - `r_air_passengers_closed_form_points_match_forecast_core`: proves the
//!   harness end-to-end by asserting `anofox_fcst_core::forecast::forecast`
//!   Naive/SeasonalNaive/RandomWalkDrift point forecasts against R.

mod common;

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use common::{all_fixture_files, assert_vec_close, f64_vec, load_fixture, num, repo_root};
use serde_json::Value;

#[test]
fn fixture_provenance_contract() {
    let files = all_fixture_files();
    assert!(
        !files.is_empty(),
        "fixture_provenance_contract: expected at least one file under validation/reference/fixtures/"
    );

    // Group by directory so we can cross-check csv <-> json siblings.
    use std::collections::BTreeMap;
    let mut by_dir: BTreeMap<std::path::PathBuf, Vec<std::path::PathBuf>> = BTreeMap::new();
    for f in &files {
        by_dir
            .entry(f.parent().unwrap().to_path_buf())
            .or_default()
            .push(f.clone());
    }

    for f in &files {
        let ext = f.extension().and_then(|e| e.to_str()).unwrap_or("");
        match ext {
            "json" => {
                let text = std::fs::read_to_string(f).unwrap_or_else(|e| {
                    panic!(
                        "fixture_provenance_contract: cannot read {}: {e}",
                        f.display()
                    )
                });
                let v: Value = serde_json::from_str(&text).unwrap_or_else(|e| {
                    panic!(
                        "fixture_provenance_contract: cannot parse {}: {e}",
                        f.display()
                    )
                });

                let obj = v.as_object().unwrap_or_else(|| {
                    panic!(
                        "fixture_provenance_contract: {} is not a JSON object",
                        f.display()
                    )
                });

                let parent_dir_name = f
                    .parent()
                    .and_then(|p| p.file_name())
                    .and_then(|n| n.to_str())
                    .unwrap_or_default();

                let family = obj
                    .get("family")
                    .and_then(|x| x.as_str())
                    .unwrap_or_else(|| {
                        panic!(
                            "fixture_provenance_contract: {} missing string 'family'",
                            f.display()
                        )
                    });
                assert_eq!(
                    family,
                    parent_dir_name,
                    "fixture_provenance_contract: {} family {:?} != parent dir name {:?}",
                    f.display(),
                    family,
                    parent_dir_name
                );

                let generator = obj
                    .get("generator")
                    .and_then(|x| x.as_str())
                    .unwrap_or_else(|| {
                        panic!(
                            "fixture_provenance_contract: {} missing string 'generator'",
                            f.display()
                        )
                    });
                let generator_path = repo_root().join(generator);
                assert!(
                    generator_path.exists(),
                    "fixture_provenance_contract: {} names generator {:?} which does not exist at {}",
                    f.display(),
                    generator,
                    generator_path.display()
                );

                let tool_versions = obj
                    .get("tool_versions")
                    .and_then(|x| x.as_object())
                    .unwrap_or_else(|| {
                        panic!(
                            "fixture_provenance_contract: {} missing object 'tool_versions'",
                            f.display()
                        )
                    });
                assert!(
                    !tool_versions.is_empty(),
                    "fixture_provenance_contract: {} has empty 'tool_versions'",
                    f.display()
                );

                let tolerance = obj.get("tolerance").and_then(|x| x.as_object());
                assert!(
                    tolerance.is_some(),
                    "fixture_provenance_contract: {} missing object 'tolerance'",
                    f.display()
                );
            }
            "csv" => {
                let dir = f.parent().unwrap().to_path_buf();
                let siblings = by_dir.get(&dir).cloned().unwrap_or_default();
                let has_json_sibling = siblings
                    .iter()
                    .any(|s| s.extension().and_then(|e| e.to_str()) == Some("json"));
                assert!(
                    has_json_sibling,
                    "fixture_provenance_contract: {} has no sibling *.json in {}",
                    f.display(),
                    dir.display()
                );
            }
            _ => {}
        }
    }
}

#[test]
fn r_air_passengers_closed_form_points_match_forecast_core() {
    let fx = load_fixture("r_forecast/air_passengers.json");

    let values: Vec<Option<f64>> = f64_vec(&fx["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();
    let frequency = num(&fx["inputs"]["frequency"]) as usize;
    let horizon = num(&fx["parameters"]["horizon"]) as usize;
    let abs_tol = common::tolerance(&fx, "closed_form_abs");

    let cases: [(&str, ModelType, usize); 3] = [
        ("naive", ModelType::Naive, 0),
        ("snaive", ModelType::SeasonalNaive, frequency),
        ("rwf_drift", ModelType::RandomWalkDrift, 0),
    ];

    for (key, model, seasonal_period) in cases {
        let options = ForecastOptions {
            model,
            horizon,
            seasonal_period,
            auto_detect_seasonality: false,
            ..Default::default()
        };
        let out = forecast(&values, &options)
            .unwrap_or_else(|e| panic!("forecast() failed for model {key:?}: {e:?}"));

        let want = f64_vec(&fx["models"][key]["point"]);
        assert_vec_close(&out.point, &want, abs_tol, 0.0, key);
    }

    // NOTE: interval parity (lower_80/upper_80/lower_95/upper_95) is NOT
    // asserted here. The wrapper's intervals are known-fabricated (audit
    // A1) and are fixed in Phase 10; interval parity against this same
    // fixture is asserted by Phase 10/17 consumers.
}
