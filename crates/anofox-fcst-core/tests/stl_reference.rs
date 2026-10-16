//! Reference-fixture assertions for the R-faithful STL port, the supsmu port, and
//! `stats::stl_strengths` (Phase 13, plan 13-02: STAT-02, D-02).
//!
//! Fixture: `r_stl_strength/cases.json` (`validation/reference/r/gen_stl_strength_reference.R`).
//!
//! - `single_period_cases_match_r_stl_components_and_strengths`: asserts `stl::stl`'s
//!   trend/seasonal/remainder against `stats::stl(ts(x, frequency=p), s.window=11)` for
//!   every single-period case, and `stats::stl_strengths` against
//!   `tsfeatures::stl_features` for the same cases.
//! - `msts_case_matches_forecast_mstl_and_per_period_strengths`: asserts the two-period
//!   `forecast::mstl` iteration (trend/per-period seasonal/remainder, and per-period
//!   seasonal strength) against `msts_7_28`.
//! - `non_seasonal_cases_match_r_supsmu_and_strengths`: asserts `supsmu::supsmu`'s trend
//!   and `stats::stl_strengths`'s trend/seasonality for the non-seasonal, short, and
//!   degenerate cases.

mod common;

use anofox_fcst_core::stats::{stl_strengths, StlStrengths};
use anofox_fcst_core::stl::{stl, StlParams};
use anofox_fcst_core::supsmu::supsmu;
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, num, tolerance};
use serde_json::Value;

/// `v["tsfeatures"][key]`, where a JSON `null` means "R returned `NA`" -> `None`.
fn opt_num(v: &Value, key: &str) -> Option<f64> {
    let field = v.get(key)?;
    if field.is_null() {
        return None;
    }
    Some(num(field))
}

fn assert_opt_close(got: Option<f64>, want: Option<f64>, abs_tol: f64, ctx: &str) {
    match (got, want) {
        (None, None) => {}
        (Some(g), Some(w)) => assert_close(g, w, abs_tol, 0.0, ctx),
        _ => panic!("{ctx}: got {got:?}, want {want:?}"),
    }
}

#[test]
fn single_period_cases_match_r_stl_components_and_strengths() {
    let fx = load_fixture("r_stl_strength/cases.json");
    let component_abs = tolerance(&fx, "component_abs");
    let strength_abs = tolerance(&fx, "strength_abs");

    let cases = fx
        .get("cases")
        .and_then(|c| c.as_object())
        .expect("r_stl_strength/cases.json: 'cases' is not an object");

    for name in ["audit_rw150", "air", "ref_rw200", "det_seasonal84"] {
        let case = cases
            .get(name)
            .unwrap_or_else(|| panic!("r_stl_strength/cases.json: missing case {name:?}"));
        let values = f64_vec(&case["values"]);
        let period = case["periods"][0].as_u64().unwrap() as usize;

        let params = StlParams::r_default(period, 11);
        let fit = stl(&values, &params).unwrap_or_else(|e| panic!("{name}: stl() failed: {e:?}"));

        let stl_fx = &case["stl"];
        assert_vec_close(
            &fit.seasonal,
            &f64_vec(&stl_fx["seasonal"]),
            component_abs,
            0.0,
            &format!("{name}: seasonal"),
        );
        assert_vec_close(
            &fit.trend,
            &f64_vec(&stl_fx["trend"]),
            component_abs,
            0.0,
            &format!("{name}: trend"),
        );
        assert_vec_close(
            &fit.remainder,
            &f64_vec(&stl_fx["remainder"]),
            component_abs,
            0.0,
            &format!("{name}: remainder"),
        );

        let StlStrengths {
            trend_strength,
            seasonality_strength,
        } = stl_strengths(&values, &[period])
            .unwrap_or_else(|e| panic!("{name}: stl_strengths() failed: {e:?}"));

        let want_trend = opt_num(&case["tsfeatures"], "trend");
        let want_seasonal = opt_num(&case["tsfeatures"], "seasonal_strength");
        assert_opt_close(
            trend_strength,
            want_trend,
            strength_abs,
            &format!("{name}: trend_strength"),
        );
        assert_opt_close(
            seasonality_strength,
            want_seasonal,
            strength_abs,
            &format!("{name}: seasonality_strength"),
        );
    }
}

#[test]
fn msts_case_matches_forecast_mstl_and_per_period_strengths() {
    let fx = load_fixture("r_stl_strength/cases.json");
    let component_abs = tolerance(&fx, "component_abs");
    let strength_abs = tolerance(&fx, "strength_abs");

    let case = &fx["cases"]["msts_7_28"];
    let values = f64_vec(&case["values"]);
    let periods = [7usize, 28usize];

    // Replicate the forecast::mstl iteration (2 passes, s.window = 7 + 4*(pos+1) by
    // ascending period position) directly against the public stl() primitive, so the
    // per-period seasonal/remainder (not exposed by stl_strengths, which only returns
    // the max F_S_i) can be checked against tsfeatures' seasonal_strength1/2 (plan
    // 13-02 task 2 acceptance criteria).
    let n = values.len();
    let mut seas = vec![vec![0.0f64; n]; periods.len()];
    let mut deseas = values.clone();
    let mut last_trend = vec![0.0f64; n];
    for _pass in 0..2 {
        for (pos, &period) in periods.iter().enumerate() {
            // deseas[i] is updated from seas[pos][i] (two parallel arrays
            // indexed by the same i); an enumerate()-based rewrite would not
            // simplify this.
            #[allow(clippy::needless_range_loop)]
            for i in 0..n {
                deseas[i] += seas[pos][i];
            }
            let s_window = 7 + 4 * (pos + 1);
            let params = StlParams::r_default(period, s_window);
            let fit = stl(&deseas, &params).unwrap();
            seas[pos] = fit.seasonal;
            last_trend = fit.trend;
            #[allow(clippy::needless_range_loop)]
            for i in 0..n {
                deseas[i] -= seas[pos][i];
            }
        }
    }
    let remainder: Vec<f64> = (0..n).map(|i| deseas[i] - last_trend[i]).collect();

    let mstl_fx = &case["mstl"];
    assert_vec_close(
        &last_trend,
        &f64_vec(&mstl_fx["trend"]),
        component_abs,
        0.0,
        "msts_7_28: trend",
    );
    assert_vec_close(
        &remainder,
        &f64_vec(&mstl_fx["remainder"]),
        component_abs,
        0.0,
        "msts_7_28: remainder",
    );
    assert_vec_close(
        &seas[0],
        &f64_vec(&mstl_fx["seasonal"]["Seasonal7"]),
        component_abs,
        0.0,
        "msts_7_28: seasonal[7]",
    );
    assert_vec_close(
        &seas[1],
        &f64_vec(&mstl_fx["seasonal"]["Seasonal28"]),
        component_abs,
        0.0,
        "msts_7_28: seasonal[28]",
    );

    // Per-period F_S_i, computed with the same `tsfeatures::stl_features` rule
    // (max(0, min(1, 1 - Var(R)/Var(R+S_i)))), against seasonal_strength1/2.
    let sample_var = |x: &[f64]| -> f64 {
        let m = x.len() as f64;
        let mean = x.iter().sum::<f64>() / m;
        x.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (m - 1.0)
    };
    let vare = sample_var(&remainder);
    for (pos, label) in [(0, "seasonal_strength1"), (1, "seasonal_strength2")] {
        let remainder_plus: Vec<f64> = (0..n).map(|i| remainder[i] + seas[pos][i]).collect();
        let var_rp = sample_var(&remainder_plus);
        let fs = (1.0 - vare / var_rp).clamp(0.0, 1.0);
        let want = num(&case["tsfeatures"]["seasonal_strength"][label]);
        assert_close(fs, want, strength_abs, 0.0, &format!("msts_7_28: {label}"));
    }

    // stl_strengths() reports the MAX over the per-period strengths (not exposed
    // individually) plus the overall trend strength.
    let want_trend = num(&case["tsfeatures"]["trend"]);
    let want_max_seasonal = [
        num(&case["tsfeatures"]["seasonal_strength"]["seasonal_strength1"]),
        num(&case["tsfeatures"]["seasonal_strength"]["seasonal_strength2"]),
    ]
    .into_iter()
    .fold(f64::MIN, f64::max);

    let got = stl_strengths(&values, &periods).unwrap();
    assert_close(
        got.trend_strength.unwrap(),
        want_trend,
        strength_abs,
        0.0,
        "msts_7_28: stl_strengths trend_strength",
    );
    assert_close(
        got.seasonality_strength.unwrap(),
        want_max_seasonal,
        strength_abs,
        0.0,
        "msts_7_28: stl_strengths seasonality_strength (max over periods)",
    );
}

#[test]
fn non_seasonal_cases_match_r_supsmu_and_strengths() {
    let fx = load_fixture("r_stl_strength/cases.json");
    let strength_abs = tolerance(&fx, "strength_abs");
    let supsmu_abs = tolerance(&fx, "supsmu_abs");

    let cases = fx
        .get("cases")
        .and_then(|c| c.as_object())
        .expect("r_stl_strength/cases.json: 'cases' is not an object");

    // (case name, periods passed to stl_strengths — mirrors how compute_ts_stats would
    // resolve seasonal_period for each: det_trend60's declared_period=1 means "none";
    // wn100 has no declared period; short_seasonal14's explicit period 7 is filtered
    // out by stl_strengths' own n > 2*period guard (14 !> 14); constant_48's period 12
    // IS usable (48 > 24) but degenerates to trend 0 / seasonality None.
    let specs: [(&str, &[usize]); 4] = [
        ("det_trend60", &[]),
        ("wn100", &[]),
        ("short_seasonal14", &[7]),
        ("constant_48", &[12]),
    ];

    for (name, periods) in specs {
        let case = cases
            .get(name)
            .unwrap_or_else(|| panic!("r_stl_strength/cases.json: missing case {name:?}"));
        let values = f64_vec(&case["values"]);
        let n = values.len();

        if let Some(supsmu_fx) = case.get("supsmu") {
            let x_idx: Vec<f64> = (1..=n).map(|i| i as f64).collect();
            let trend = supsmu(&x_idx, &values)
                .unwrap_or_else(|e| panic!("{name}: supsmu() failed: {e:?}"));
            assert_vec_close(
                &trend,
                &f64_vec(&supsmu_fx["trend"]),
                supsmu_abs,
                0.0,
                &format!("{name}: supsmu trend"),
            );
        }

        let StlStrengths {
            trend_strength,
            seasonality_strength,
        } = stl_strengths(&values, periods)
            .unwrap_or_else(|e| panic!("{name}: stl_strengths() failed: {e:?}"));

        let want_trend = opt_num(&case["tsfeatures"], "trend");
        assert_opt_close(
            trend_strength,
            want_trend,
            strength_abs,
            &format!("{name}: trend_strength"),
        );
        assert_eq!(
            seasonality_strength, None,
            "{name}: expected seasonality_strength None (no usable period)"
        );
    }
}
