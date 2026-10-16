# RC-03: AutoETS's optimiser converges to a worse-than-R optimum for ETS(M,Ad,M) on AirPassengers

**Title:** `ETS::new(ETSSpec::madm(), ...)` fit converges to a strictly worse log-likelihood
than R's `ets()` on AirPassengers, so `AutoETS` selects `ETS(M,A,M)` where R selects
`ETS(M,Ad,M)`

**Summary:** On the classic AirPassengers series, R's `forecast::ets()` selects `ETS(M,Ad,M)`
with log-likelihood `-679.583216` and AICc `1400.6384`. `anofox-forecast`'s own optimiser,
fitting the identical spec (`ETS(M,Ad,M)`, period 12) on the same data, converges to
log-likelihood `-683.964133` (AICc `1409.4003`) — strictly worse by `4.38` nats. Because that
candidate scores worse than `ETS(M,A,M)` under the crate's own objective
(`crate_aicc=1401.0677` for `M,A,M` vs `1409.4003` for `M,Ad,M`), `AutoETS::with_config(...)`
selects `ETS(M,A,M)` instead of R's `ETS(M,Ad,M)`.

**Versions:** anofox-forecast 0.16.1, rustc 1.89.0, R 4.6.1, forecast 9.0.2.

**Expected (R):**
- `ets(AirPassengers)` selects `ETS(M,Ad,M)`, `loglik = -679.583216`, `aicc = 1400.6384`,
  fitted parameters `alpha = 0.8024211075374774`, `beta = 0.019688598829206226`,
  `gamma = 0.19757889246252258`, `phi = 0.98` (all within R's documented bounds: alpha, beta,
  gamma in `[1e-4, 0.9999]`, `beta <= alpha`, `gamma <= 1 - alpha`, `phi` in `[0.8, 0.98]`).

**Actual (crate):**
- `ETS::new(ETSSpec::madm(), 12).fit(&ts)` on the same 144 values converges to
  `loglik = -683.964133` (gap `-4.380917` vs R, i.e. worse), even though the crate's own ETS
  recursion evaluated AT R's fitted parameters reproduces R's log-likelihood and forecast
  bit-for-bit (confirmed separately: `with_params_and_states` at these exact parameters gives
  `loglik_rel_dev=3.332e-16`, `point_rel_dev=1.388e-15`). The discrepancy is isolated to the
  optimiser's search, not the likelihood or recursion formula.

**Minimal reproduction:**

```rust
// Cargo.toml: anofox-forecast = "0.16.1"
fn main() {
    use anofox_forecast::core::TimeSeries;
    use anofox_forecast::models::exponential::{ETSSpec, ETS as ETSModel};
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

    // R's ets(AirPassengers) selects ETS(M,Ad,M) at loglik -679.583216 (R 4.6.1,
    // forecast 9.0.2). The crate's own optimiser, fitting the same spec on the
    // same data, converges to a strictly worse log-likelihood.
    let mut model = ETSModel::new(ETSSpec::madm(), 12);
    model.fit(&ts).expect("ETS(M,Ad,M) fit");
    let crate_loglik = model.log_likelihood().expect("log_likelihood available");
    let r_loglik = -679.583216_f64;

    println!("RC-03 repro: crate_loglik={crate_loglik:.6} r_loglik={r_loglik:.6}");
    assert!(
        crate_loglik < r_loglik - 1e-6,
        "expected the crate's ETS(M,Ad,M) optimiser to score worse than R's: crate={crate_loglik} r={r_loglik}"
    );
}
```

**Ladder step that isolates the cause:** E6 (crate's own fit vs R, with E5a/E5b confirming the
likelihood/recursion formula agree at R's parameters). E7 (`AutoETS::model_scores()` AICc per
candidate) shows the same crate-vs-R ordering flip only for the `M,Ad,M` candidate.

**Suggested direction:** multi-start the damped-trend, multiplicative-season (`M,Ad,M`)
Nelder-Mead search with additional starting points (e.g. seeded from the undamped `M,A,M`
optimum, or from a grid over `phi` near `[0.8, 0.98]`), since the crate currently finds the
undamped optimum reliably but under-explores the damped region for this spec.
