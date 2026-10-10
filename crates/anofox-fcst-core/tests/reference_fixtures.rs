//! Reference-fixture harness (TEST-01).
//!
//! Six tests:
//! - `fixture_provenance_contract`: guards every fixture file (present and
//!   future) for the required provenance keys.
//! - `r_air_passengers_closed_form_points_match_forecast_core`: proves the
//!   harness end-to-end by asserting `anofox_fcst_core::forecast::forecast`
//!   Naive/SeasonalNaive/RandomWalkDrift point forecasts against R.
//! - `r_random_walk_closed_form_points_match_forecast_core`: same proof on
//!   the seeded random-walk series (Naive / RandomWalkDrift).
//! - `scipy_moments_fixture_is_self_consistent`: recomputes G1/G2/BC from
//!   each vector's raw data and checks it against the Python-recorded
//!   values -- this is the exact definition `ts_stats`'s moments pin against
//!   (see `tests/stats_reference.rs`).
//! - `multiscale_ols_fixture_is_self_consistent`: checks `[1 | X] . coef`
//!   reproduces `fitted` for every case, and that the CSV companion's
//!   future rows reproduce `[1, x] . coef` for `exog_typical`.
//! - `panel_nan_guard_fixture_is_well_formed`: checks shape/outcome-class
//!   well-formedness of the DEP-02 panel guard fixture.

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

#[test]
fn r_random_walk_closed_form_points_match_forecast_core() {
    let fx = load_fixture("r_forecast/simulated_series.json");

    let values: Vec<Option<f64>> = f64_vec(&fx["series"]["rw"]["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();
    let abs_tol = common::tolerance(&fx, "closed_form_abs");

    let cases: [(&str, ModelType); 2] = [
        ("naive", ModelType::Naive),
        ("rwf_drift", ModelType::RandomWalkDrift),
    ];

    for (key, model) in cases {
        let horizon = f64_vec(&fx["series"]["rw"]["models"][key]["point"]).len();
        let options = ForecastOptions {
            model,
            horizon,
            seasonal_period: 0,
            auto_detect_seasonality: false,
            ..Default::default()
        };
        let out = forecast(&values, &options)
            .unwrap_or_else(|e| panic!("forecast() failed for model {key:?}: {e:?}"));

        let want = f64_vec(&fx["series"]["rw"]["models"][key]["point"]);
        assert_vec_close(&out.point, &want, abs_tol, 0.0, key);
    }
}

/// Recomputes the bias-corrected skewness (G1), excess kurtosis (G2), and
/// the SAS bimodality coefficient (BC) from raw sample data using the
/// closed-form definitions scipy's `bias=False` variants implement. This is
/// the exact definition `ts_stats`'s moments reproduce (pinned separately in
/// `tests/stats_reference.rs`) — it is NOT itself a product-surface assertion.
fn population_moments_g1_g2_bc(x: &[f64]) -> (f64, f64, f64) {
    let n = x.len() as f64;
    let mean = x.iter().sum::<f64>() / n;
    let m2 = x.iter().map(|xi| (xi - mean).powi(2)).sum::<f64>() / n;
    let m3 = x.iter().map(|xi| (xi - mean).powi(3)).sum::<f64>() / n;
    let m4 = x.iter().map(|xi| (xi - mean).powi(4)).sum::<f64>() / n;

    let g1 = m3 / m2.powf(1.5);
    let big_g1 = g1 * (n * (n - 1.0)).sqrt() / (n - 2.0);

    let g2 = m4 / m2.powi(2) - 3.0;
    let big_g2 = ((n + 1.0) * g2 + 6.0) * (n - 1.0) / ((n - 2.0) * (n - 3.0));

    let bc = (big_g1.powi(2) + 1.0) / (big_g2 + 3.0 * (n - 1.0).powi(2) / ((n - 2.0) * (n - 3.0)));

    (big_g1, big_g2, bc)
}

#[test]
fn scipy_moments_fixture_is_self_consistent() {
    let fx = load_fixture("moments/scipy_moments.json");
    let abs_tol = common::tolerance(&fx, "abs");
    let rel_tol = common::tolerance(&fx, "rel");

    let vectors = fx["vectors"]
        .as_object()
        .expect("scipy_moments_fixture_is_self_consistent: 'vectors' is not an object");
    assert!(
        !vectors.is_empty(),
        "scipy_moments_fixture_is_self_consistent: 'vectors' is empty"
    );

    for (name, v) in vectors {
        let x = f64_vec(&v["x"]);
        assert!(
            x.len() > 3,
            "{name}: need more than 3 observations for the G2/BC formulas, got {}",
            x.len()
        );

        let (g1, g2, bc) = population_moments_g1_g2_bc(&x);

        common::assert_close(
            g1,
            num(&v["skew_bias_false"]),
            abs_tol,
            rel_tol,
            &format!("{name}.skew_bias_false"),
        );
        common::assert_close(
            g2,
            num(&v["kurtosis_excess_bias_false"]),
            abs_tol,
            rel_tol,
            &format!("{name}.kurtosis_excess_bias_false"),
        );
        common::assert_close(
            bc,
            num(&v["bimodality_coefficient_sas"]),
            abs_tol,
            rel_tol,
            &format!("{name}.bimodality_coefficient_sas"),
        );
    }
}

#[test]
fn multiscale_ols_fixture_is_self_consistent() {
    let fx = load_fixture("multiscale_ols/cases.json");
    let coef_abs = common::tolerance(&fx, "coef_abs");
    let fitted_abs_per_sd = common::tolerance(&fx, "fitted_abs_per_sd");

    let cases = fx["cases"]
        .as_object()
        .expect("multiscale_ols_fixture_is_self_consistent: 'cases' is not an object");
    assert!(
        !cases.is_empty(),
        "multiscale_ols_fixture_is_self_consistent: 'cases' is empty"
    );

    for (name, case) in cases {
        let n = num(&case["n"]) as usize;
        let p = num(&case["p"]) as usize;

        let x_cols: Vec<Vec<f64>> = case["X"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'X' is not an array"))
            .iter()
            .map(f64_vec)
            .collect();
        assert_eq!(
            x_cols.len(),
            p,
            "{name}: X has {} columns, want p={p}",
            x_cols.len()
        );
        for (j, col) in x_cols.iter().enumerate() {
            assert_eq!(
                col.len(),
                n,
                "{name}: X[{j}] has length {}, want n={n}",
                col.len()
            );
        }

        let y = f64_vec(&case["y"]);
        let coef = f64_vec(&case["coef"]);
        let fitted = f64_vec(&case["fitted"]);
        assert_eq!(y.len(), n, "{name}: y length {} != n={n}", y.len());
        assert_eq!(
            fitted.len(),
            n,
            "{name}: fitted length {} != n={n}",
            fitted.len()
        );
        assert_eq!(
            coef.len(),
            p + 1,
            "{name}: coef length {} != p+1={}",
            coef.len(),
            p + 1
        );

        assert!(
            y.len() > 1,
            "{name}: need at least 2 observations to compute var_y"
        );
        let mean_y = y.iter().sum::<f64>() / y.len() as f64;
        let var_y = y.iter().map(|yi| (yi - mean_y).powi(2)).sum::<f64>() / (y.len() as f64 - 1.0);
        let fit_tol = fitted_abs_per_sd * var_y.sqrt();

        for i in 0..n {
            let mut recomputed = coef[0];
            for (j, col) in x_cols.iter().enumerate() {
                recomputed += coef[j + 1] * col[i];
            }
            common::assert_close(
                recomputed,
                fitted[i],
                fit_tol,
                0.0,
                &format!("{name}.fitted[{i}]"),
            );
        }
    }

    // The exog_typical CSV companion: 365 history rows + 5 future rows,
    // whose future expected_regression equals [1, x] . coef.
    let exog_coef = f64_vec(&fx["cases"]["exog_typical"]["coef"]);
    let csv_path = common::fixtures_dir().join("multiscale_ols/exog_typical.csv");
    let csv_text = std::fs::read_to_string(&csv_path).unwrap_or_else(|e| {
        panic!(
            "multiscale_ols_fixture_is_self_consistent: cannot read {}: {e}",
            csv_path.display()
        )
    });

    let mut lines = csv_text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("exog_typical.csv: missing header row")
        .split(',')
        .collect();
    let col_idx = |col: &str| {
        header
            .iter()
            .position(|h| *h == col)
            .unwrap_or_else(|| panic!("exog_typical.csv: missing column {col:?}"))
    };
    let split_idx = col_idx("split");
    let x_idx: [usize; 4] = [col_idx("x1"), col_idx("x2"), col_idx("x3"), col_idx("x4")];
    let expected_idx = col_idx("expected_regression");

    let mut history_count = 0usize;
    let mut future_count = 0usize;
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let fields: Vec<&str> = line.split(',').collect();
        match fields[split_idx] {
            "history" => history_count += 1,
            "future" => {
                future_count += 1;
                let mut recomputed = exog_coef[0];
                for (j, &idx) in x_idx.iter().enumerate() {
                    let xi: f64 = fields[idx].parse().unwrap_or_else(|e| {
                        panic!(
                            "exog_typical.csv: cannot parse x{} value {:?}: {e}",
                            j + 1,
                            fields[idx]
                        )
                    });
                    recomputed += exog_coef[j + 1] * xi;
                }
                let expected: f64 = fields[expected_idx].parse().unwrap_or_else(|e| {
                    panic!(
                        "exog_typical.csv: cannot parse expected_regression {:?}: {e}",
                        fields[expected_idx]
                    )
                });
                common::assert_close(
                    recomputed,
                    expected,
                    coef_abs,
                    0.0,
                    "exog_typical.csv future row: [1, x] . coef vs expected_regression",
                );
            }
            other => panic!("exog_typical.csv: unexpected split value {other:?}"),
        }
    }
    assert_eq!(
        history_count, 365,
        "exog_typical.csv: want 365 history rows, got {history_count}"
    );
    assert_eq!(
        future_count, 5,
        "exog_typical.csv: want 5 future rows, got {future_count}"
    );
}

#[test]
fn panel_nan_guard_fixture_is_well_formed() {
    const ALLOWED_METHODS: [&str; 3] = ["GlobalETS", "GlobalTheta", "GlobalCroston"];
    const ALLOWED_OUTCOMES: [&str; 2] = ["finite", "error"];

    let fx = load_fixture("panel_nan_guard/cases.json");
    let cases = fx["cases"]
        .as_object()
        .expect("panel_nan_guard_fixture_is_well_formed: 'cases' is not an object");
    assert!(
        !cases.is_empty(),
        "panel_nan_guard_fixture_is_well_formed: 'cases' is empty"
    );

    for (name, case) in cases {
        let n_series = num(&case["n_series"]) as usize;
        let series_len = num(&case["series_len"]) as usize;

        let values = case["values"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'values' is not an array"));
        assert_eq!(
            values.len(),
            n_series,
            "{name}: values has {} series, want n_series={n_series}",
            values.len()
        );
        for (s, row) in values.iter().enumerate() {
            let row_vals = f64_vec(row);
            assert_eq!(
                row_vals.len(),
                series_len,
                "{name}: series {s} has length {}, want series_len={series_len}",
                row_vals.len()
            );
        }

        let expect = case["expect"]
            .as_object()
            .unwrap_or_else(|| panic!("{name}: 'expect' is not an object"));
        assert!(!expect.is_empty(), "{name}: 'expect' is empty");
        for (method, outcome) in expect {
            assert!(
                ALLOWED_METHODS.contains(&method.as_str()),
                "{name}: unexpected method {method:?}, want one of {ALLOWED_METHODS:?}"
            );
            let outcome_str = outcome
                .as_str()
                .unwrap_or_else(|| panic!("{name}.{method}: 'expect' value is not a string"));
            assert!(
                ALLOWED_OUTCOMES.contains(&outcome_str),
                "{name}.{method}: unexpected outcome {outcome_str:?}, want one of {ALLOWED_OUTCOMES:?}"
            );
        }
    }

    let interior_gaps_nan_count: usize = cases["interior_gaps"]["values"]
        .as_array()
        .expect("panel_nan_guard_fixture_is_well_formed: missing 'interior_gaps' case")
        .iter()
        .flat_map(f64_vec)
        .filter(|v| v.is_nan())
        .count();
    assert!(
        interior_gaps_nan_count >= 1,
        "interior_gaps: expected at least one NaN, decoded {interior_gaps_nan_count}"
    );

    let infinite_value_inf_count: usize = cases["infinite_value"]["values"]
        .as_array()
        .expect("panel_nan_guard_fixture_is_well_formed: missing 'infinite_value' case")
        .iter()
        .flat_map(f64_vec)
        .filter(|v| v.is_infinite() && *v > 0.0)
        .count();
    assert_eq!(
        infinite_value_inf_count, 1,
        "infinite_value: expected exactly one +inf, decoded {infinite_value_inf_count}"
    );
}
