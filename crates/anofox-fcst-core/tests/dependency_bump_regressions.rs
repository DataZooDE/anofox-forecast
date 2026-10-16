//! MFLES #219 bounded-forecast guard (DEP-02 proof point).
//!
//! Regression guard for sipemu/anofox-forecast#219: pre-0.15.10, MFLES's
//! auto multiplicative-mode selection on a near-zero-value series produced
//! a runaway forecast (~13x level). Fixed in 0.15.10 via an additive
//! fallback for near-zero series, log winsorizing, and a back-transform
//! clamp. This test pins the fixed (bounded) behaviour on the bumped tree.

mod common;

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};

#[test]
fn mfles_issue_219_forecasts_stay_bounded() {
    // The 24-point upstream reproducer (level ~2000, near-zero outlier at
    // index 11), shared with forecast_snapshot.rs's issue_219 series — the
    // reproducer lives in exactly one place (the committed fixture).
    let fx = common::load_fixture("forecast_snapshot/point_forecasts.json");
    let values = common::f64_vec(&fx["series"]["issue_219"]["values"]);
    assert_eq!(
        values.len(),
        24,
        "issue_219 reproducer should have 24 points"
    );

    let mut sorted = values.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let median = (sorted[11] + sorted[12]) / 2.0;
    let bound = 2.5 * median;

    for model in [ModelType::MFLES, ModelType::AutoMFLES] {
        let input: Vec<Option<f64>> = values.iter().copied().map(Some).collect();
        let options = ForecastOptions {
            model,
            horizon: 12,
            seasonal_period: 12,
            auto_detect_seasonality: false,
            ..Default::default()
        };

        let output = forecast(&input, &options)
            .unwrap_or_else(|e| panic!("forecast() failed for {:?}: {e:?}", model));

        let max = output
            .point
            .iter()
            .copied()
            .fold(f64::NEG_INFINITY, f64::max);
        let min = output.point.iter().copied().fold(f64::INFINITY, f64::min);

        assert!(
            output.point.iter().all(|p| p.is_finite()),
            "{:?}: expected all-finite points, got {:?}",
            model,
            output.point
        );
        assert!(
            max < bound,
            "{:?}: forecast_max={max} >= bound={bound} (2.5 * median={median}); points={:?}",
            model,
            output.point
        );
        assert!(
            min > 0.0,
            "{:?}: forecast_min={min} <= 0; points={:?}",
            model,
            output.point
        );
    }
}
