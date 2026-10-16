# RC-04: SimpleExponentialSmoothing's optimiser converges to a worse-than-R optimum on a
zero-inflated intermittent series

**Title:** `SimpleExponentialSmoothing::auto()` / `ETS::new(ETSSpec::ann(), ...)` converges to
a strictly worse log-likelihood than R's `ses()` on an intermittent (mostly-zero) series

**Summary:** On a seeded intermittent demand series (120 points, about 70% zeros), R's
`forecast::ses()` fits `alpha = 0.000100000835353129`, `l0 = 1.24173768106333`, log-likelihood
`-374.538408`. The crate's own optimiser for the same `ETS(A,N,N)` spec converges to
`alpha = 0.139114`, with its initial level fixed to the first observation (`l0 = 4`), and
log-likelihood `-380.625612` — strictly worse by `6.09` nats. Evaluating the crate's own
recursion AT R's parameters (`with_params_and_states`) reproduces R's exact log-likelihood
(`loglik_rel_dev = 0.000e0`), so the recursion and likelihood formula agree with R; the
optimiser is converging to a point its own objective can see is worse.

**Versions:** anofox-forecast 0.16.1, rustc 1.89.0, R 4.6.1, forecast 9.0.2.

**Expected (R):** `ses(inter)` fits `alpha = 0.000100000835353129`, `l0 = 1.24173768106333`,
`loglik = -374.538408`.

**Actual (crate):** `SimpleExponentialSmoothing::auto()` / `ETS::new(ETSSpec::ann(), 1).fit()`
on the identical 120 values converges to `alpha = 0.139114`, `l0` fixed to the series' first
observation (`4.0`), `loglik = -380.625612`, which is worse than R's parameters evaluated
under the crate's own likelihood (`-374.538408`).

**Minimal reproduction:**

```rust
// Cargo.toml: anofox-forecast = "0.16.1"
fn main() {
    use anofox_forecast::core::TimeSeries;
    use anofox_forecast::models::exponential::{ETSSpec, ETS as ETSModel};
    use anofox_forecast::prelude::Forecaster;
    use chrono::{Duration, TimeZone, Utc};

    let inter: Vec<f64> = vec![
        4.0, 3.0, 0.0, 4.0, 0.0, 4.0, 0.0, 0.0, 0.0, 6.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        6.0, 0.0, 0.0, 0.0, 2.0, 0.0, 3.0, 0.0, 5.0, 0.0, 0.0, 3.0, 0.0, 0.0, 5.0, 4.0, 0.0, 3.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0, 3.0, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 2.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 8.0, 0.0, 5.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0, 0.0, 4.0, 0.0,
        5.0, 0.0, 4.0, 0.0, 7.0, 3.0, 5.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 4.0, 4.0, 0.0, 3.0, 4.0, 7.0, 0.0,
    ];
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..inter.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    let ts = TimeSeries::univariate(timestamps, inter).expect("valid timeseries");

    // R's ses() on this intermittent series converges to alpha=0.000100000835353129,
    // l0=1.24173768106333 (R 4.6.1, forecast 9.0.2). Evaluating the crate's own
    // ETS(A,N,N) recursion AT R's parameters scores strictly better than the
    // crate's own converged optimum, so the crate's optimiser is missing a
    // higher-likelihood point its own objective can see.
    let mut own_fit = ETSModel::new(ETSSpec::ann(), 1);
    own_fit.fit(&ts).expect("own ETS(A,N,N) fit");
    let own_loglik = own_fit.log_likelihood().expect("log_likelihood available");

    let mut at_r_params = ETSModel::with_params_and_states(
        ETSSpec::ann(),
        1,
        0.000100000835353129,
        None,
        None,
        None,
        1.24173768106333,
        0.0,
        vec![],
    );
    at_r_params.fit(&ts).expect("ETS(A,N,N) at R's parameters");
    let r_params_loglik = at_r_params
        .log_likelihood()
        .expect("log_likelihood available");

    println!("RC-04 repro: own_fit_loglik={own_loglik:.6} loglik_at_r_params={r_params_loglik:.6}");
    assert!(
        own_loglik < r_params_loglik - 1e-6,
        "expected the crate's own SES optimum to score worse than R's parameters evaluated \
         under the crate's own objective: own={own_loglik} at_r_params={r_params_loglik}"
    );
}
```

**Ladder step that isolates the cause:** E4 (the crate's `SimpleExponentialSmoothing::auto()`
and the R-faithful `ETS(A,N,N)` mapping give identical deviation — not a mapping issue); E5
(crate recursion at R's parameters is bit-identical to R's own reported log-likelihood); E6
(crate's own optimiser scores worse than R's parameters under its own objective).

**Suggested direction:** the crate's SES path appears to fix the initial level `l0` to the
series' first observation rather than optimising it jointly with `alpha` (R's `ses()` jointly
optimises `l0` and `alpha` via MLE). On a zero-inflated series, fixing `l0 = values[0]` (here
`4.0`, an arbitrary non-zero spike) biases the optimiser away from R's near-zero-level optimum
(`l0 ≈ 1.24`). Making `l0` a free parameter in the optimisation (as it already is for the
general `ETS::new()` path per the `with_params_and_states` evidence above) should close most
of this gap.
