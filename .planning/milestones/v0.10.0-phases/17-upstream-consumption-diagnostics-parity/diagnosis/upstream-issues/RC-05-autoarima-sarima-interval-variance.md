> **RESOLVED (2026-10-16, Phase 18 plan 18-05):** Fixed upstream in anofox-forecast 0.16.2
> (PR #230, shipping the SARIMA/ARIMA/AutoARIMA KalmanForecast prediction-interval fix from
> PR #229 — https://github.com/sipemu/anofox-forecast/releases/tag/v0.16.2). Re-running this
> draft's own minimal reproduction (mirrored as `repro_rc_05` in `r_parity_diagnosis.rs`, now
> inverted to assert the fix instead of the defect) on 0.16.2 gives a worst crate/R half-width
> ratio of `0.9951` (was `0.818` on 0.16.1). The AirPassengers AutoARIMA 95% interval
> (`air/auto_arima/AutoARIMA` in `r_forecast_parity.rs`) re-measures at `interval_dev=4.592775e-3`
> (was `1.846662e-1`), now inside the parity tier (`interval_tol=7e-3`). D-16's one-allowed
> AutoARIMA known-gap exception (user decision 2026-10-15) is retired; every AutoARIMA check is
> hard parity. Left below unmodified as the historical record of the original diagnosis.

# RC-05: SARIMA 95% prediction intervals are systematically narrower than R `stats::arima`'s
KalmanForecast variance at the correct selected order

**Title:** `SARIMA::predict_with_intervals` under-covers R's state-space prediction-error
variance for a seasonal order on AirPassengers, even though the point forecast and the
selected order both match R

**Summary:** At R's exact `auto.arima(AirPassengers)` order, `ARIMA(2,1,1)(0,1,0)[12]`
(matched by the crate's own `AutoARIMA`, upstream PR #223), the crate's point forecast
matches R within `0.69%` relative deviation. Its 95% prediction half-widths, however, are
systematically narrower than R's — the crate/R half-width ratio falls from `1.04` at `h=1` to
`0.818` at `h=6` and only recovers to `0.879` at `h=12` — a horizon-dependent underestimate of
multi-step forecast variance, not a one-off numerical difference. Real-calendar and synthetic
timestamps give bit-identical intervals (ruling out the extension wrapper's timestamp
construction), and the crate's own `AutoARIMA` selection path reproduces the same deviation as
calling `ARIMA` explicitly at R's order (ruling out a selection-vs-estimation split): the cause
is isolated to the SARIMA prediction-error-variance computation itself.

**Versions:** anofox-forecast 0.16.1, rustc 1.89.0, R 4.6.1, forecast 9.0.2.

**Expected (R):** `forecast(auto.arima(AirPassengers), h = 12, level = 95)` 95% half-widths
(`h=1..12`): `22.5498, 26.4646, 29.7091, 31.8307, 33.3992, 34.5618, 35.4479, 36.1345, 36.6756,
37.1089, 37.4612, 37.7520` — computed via R's `stats::arima` `KalmanForecast` state-space
one-step-ahead prediction-error recursion.

**Actual (crate):** `SARIMA::new(2, 1, 1, 0, 1, 0, 12).predict_with_intervals(12, 0.95)` on the
identical 144 values gives half-widths whose ratio to R's falls to `0.818` by `h=6` (point
forecasts agree to `0.69%` relative deviation over the same horizon, so the mean recursion is
not the source).

**Minimal reproduction:**

```rust
// Cargo.toml: anofox-forecast = "0.16.1"
fn main() {
    use anofox_forecast::core::TimeSeries;
    use anofox_forecast::models::arima::SARIMA;
    use anofox_forecast::prelude::Forecaster;
    use chrono::{Months, TimeZone, Utc};

    let air: Vec<f64> = vec![
        112.0, 118.0, 132.0, 129.0, 121.0, 135.0, 148.0, 148.0, 136.0, 119.0, 104.0, 118.0, 115.0,
        126.0, 141.0, 135.0, 125.0, 149.0, 170.0, 170.0, 158.0, 133.0, 114.0, 140.0, 145.0, 150.0,
        178.0, 163.0, 172.0, 178.0, 199.0, 199.0, 184.0, 162.0, 146.0, 166.0, 171.0, 180.0, 193.0,
        181.0, 183.0, 218.0, 230.0, 242.0, 209.0, 191.0, 172.0, 194.0, 196.0, 196.0, 236.0, 235.0,
        229.0, 243.0, 264.0, 272.0, 237.0, 211.0, 180.0, 201.0, 204.0, 188.0, 235.0, 227.0, 234.0,
        264.0, 302.0, 293.0, 259.0, 229.0, 203.0, 229.0, 242.0, 233.0, 267.0, 269.0, 270.0, 315.0,
        364.0, 347.0, 312.0, 274.0, 237.0, 278.0, 284.0, 277.0, 317.0, 313.0, 318.0, 374.0, 413.0,
        405.0, 355.0, 306.0, 271.0, 306.0, 315.0, 301.0, 356.0, 348.0, 355.0, 422.0, 465.0, 467.0,
        404.0, 347.0, 305.0, 336.0, 340.0, 318.0, 362.0, 348.0, 363.0, 435.0, 491.0, 505.0, 404.0,
        359.0, 310.0, 337.0, 360.0, 342.0, 406.0, 396.0, 420.0, 472.0, 548.0, 559.0, 463.0, 407.0,
        362.0, 405.0, 417.0, 391.0, 419.0, 461.0, 472.0, 535.0, 622.0, 606.0, 508.0, 461.0, 390.0,
        432.0,
    ];
    let base = Utc.with_ymd_and_hms(1949, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..air.len())
        .map(|i| base + Months::new(i as u32))
        .collect();
    let ts = TimeSeries::univariate(timestamps, air).expect("valid timeseries");

    // R's auto.arima(AirPassengers) selects ARIMA(2,1,1)(0,1,0)[12] (R 4.6.1,
    // forecast 9.0.2). At that exact order the crate's point forecast matches
    // R within 0.69% relative deviation, but its 95% prediction half-widths
    // are systematically narrower than R's stats::arima KalmanForecast
    // variance, worsening with horizon.
    let mut model = SARIMA::new(2, 1, 1, 0, 1, 0, 12);
    model.fit(&ts).expect("SARIMA(2,1,1)(0,1,0)[12] fit");
    let forecast = model.predict_with_intervals(12, 0.95).expect("predict");
    let lower = forecast
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .expect("lower bound");
    let upper = forecast
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .expect("upper bound");

    // R's 95% half-widths at h=1..12 (forecast(auto.arima(AirPassengers)), R 4.6.1).
    let r_half: [f64; 12] = [
        22.5498, 26.4646, 29.7091, 31.8307, 33.3992, 34.5618, 35.4479, 36.1345, 36.6756, 37.1089,
        37.4612, 37.7520,
    ];
    let worst_ratio = (0..12)
        .map(|h| ((upper[h] - lower[h]) / 2.0) / r_half[h])
        .fold(f64::INFINITY, f64::min);

    println!("RC-05 repro: worst crate/R half-width ratio={worst_ratio:.4}");
    assert!(
        worst_ratio < 0.90,
        "expected the crate's SARIMA interval to be systematically narrower than R's \
         at some horizon (ratio < 0.90): worst_ratio={worst_ratio}"
    );
}
```

**Ladder step that isolates the cause:** D-15 Miss E8 (`autoarima_air_explicit_arima_e8_probe`
— calling `ARIMA` explicitly at R's exact order gives `point_dev=6.883204e-3`,
`interval_dev=2.489813e-1`, essentially the same deviation as `AutoARIMA`'s own output,
ruling out a selection-vs-estimation split); E3 (`autoarima_air_e3_timestamp_probe` — synthetic
and real-calendar timestamps give bit-identical intervals, `same_bits=true`, ruling out the
extension wrapper's synthetic index).

**Suggested direction:** compare the crate's multi-step prediction-error variance recursion
for a seasonal `SARIMA` model against R's `stats:::KalmanForecast` (or the textbook psi-weight
/ MA(∞) variance accumulation for a seasonally-differenced ARIMA), focusing on how seasonal
differencing (`D=1`, period 12) contributes to the growth of forecast-error variance with
horizon — the crate/R ratio's horizon-shape (dipping to a minimum around `h=5..7` then
partially recovering) suggests a seasonal MA-weight term is under-accumulated rather than a
constant scale-factor error.
