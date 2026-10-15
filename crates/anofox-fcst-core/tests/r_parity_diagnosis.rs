//! D-12 root-cause diagnosis ladder (Phase 17 plan 17-03, Task 1/2) plus the
//! D-15 AutoARIMA order/parity measurement.
//!
//! For each of the Task 1/2 pre-diagnosis gaps this file runs the E1-E9
//! ladder from the plan's `<interfaces>` block: it compares the extension
//! wrapper's call path (`anofox_fcst_core::forecast::forecast`) against the
//! same model built directly from `anofox-forecast` 0.16.1 (E2/E3), against
//! an R-faithful mapping (E4), and against the crate evaluated at R's own
//! fitted parameters and initial states (E5/E6/E7/E9). Every R number is
//! read from the fixture — nothing is hard-coded.
//!
//! Run with:
//!   cargo test -p anofox-fcst-core --test r_parity_diagnosis -- --include-ignored --nocapture

mod common;

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, ETSSpec, HoltLinearTrend, HoltWinters as HoltWintersModel, ModelPool,
    SeasonalType, SimpleExponentialSmoothing, ETS as ETSModel,
};
use anofox_forecast::prelude::Forecaster;
use chrono::{Duration, Months, TimeZone, Utc};
use serde_json::Value;

// ─── TimeSeries construction (mirrors forecast.rs) ──────────────────────────

/// Synthetic hourly index from 2024-01-01 — bit-for-bit what
/// `forecast.rs::make_timeseries` builds (E2 construction).
fn ts_synthetic(values: &[f64]) -> TimeSeries {
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec()).expect("valid synthetic timeseries")
}

/// Real regular monthly index (E3 construction): AirPassengers starts
/// 1949-01; the simulated series get an arbitrary monthly start (any regular
/// monthly index is sufficient per the interfaces — only regularity matters).
fn ts_real_monthly(values: &[f64], start_year: i32, start_month: u32) -> TimeSeries {
    let base = Utc
        .with_ymd_and_hms(start_year, start_month, 1, 0, 0, 0)
        .unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Months::new(i as u32))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec()).expect("valid calendar timeseries")
}

// ─── fixture access ──────────────────────────────────────────────────────────

fn air_fixture() -> Value {
    common::load_fixture("r_forecast/air_passengers.json")
}
fn sim_fixture() -> Value {
    common::load_fixture("r_forecast/simulated_series.json")
}

fn air_values() -> Vec<f64> {
    common::f64_vec(&air_fixture()["inputs"]["values"])
}
fn rw_values() -> Vec<f64> {
    common::f64_vec(&sim_fixture()["series"]["rw"]["inputs"]["values"])
}
fn inter_values() -> Vec<f64> {
    common::f64_vec(&sim_fixture()["series"]["inter"]["inputs"]["values"])
}
fn trend_values() -> Vec<f64> {
    common::f64_vec(&sim_fixture()["series"]["trend"]["inputs"]["values"])
}

fn par_num(par: &Value, key: &str) -> f64 {
    par.get(key)
        .map(common::num)
        .unwrap_or_else(|| panic!("par_num(): fixture par has no key {key:?}"))
}
fn par_opt(par: &Value, key: &str) -> Option<f64> {
    par.get(key).map(common::num)
}

/// Build the crate's seasonal-buffer ordering from R's `fit.par` (s0..s(m-2),
/// m-1 explicit values), completing the implied m-th state via the
/// normalisation constraint and ordering oldest-state-first per the
/// interfaces: `[s(m-1), s(m-2), ..., s0]`.
fn seasonal_states_from_par(par: &Value, period: usize, multiplicative: bool) -> Vec<f64> {
    let m = period;
    let mut explicit = Vec::with_capacity(m - 1);
    for i in 0..(m - 1) {
        explicit.push(par_num(par, &format!("s{i}")));
    }
    let sum: f64 = explicit.iter().sum();
    let last = if multiplicative {
        (m as f64) - sum
    } else {
        -sum
    };
    // explicit = [s0, s1, ..., s(m-2)]; full state set in R index order is
    // explicit ++ [last] = [s0, ..., s(m-2), s(m-1)]. Oldest-first order is
    // the reverse of that.
    let mut full = explicit;
    full.push(last);
    full.reverse();
    full
}

// ─── deviation metrics ───────────────────────────────────────────────────────

/// Max over h of the relative deviation |got-want|/|want| (point metric for
/// non-closed-form models, per the interfaces).
fn max_rel_dev(got: &[f64], want: &[f64]) -> f64 {
    assert_eq!(got.len(), want.len(), "length mismatch in max_rel_dev");
    got.iter()
        .zip(want.iter())
        .map(|(g, w)| {
            if w.abs() > 0.0 {
                (g - w).abs() / w.abs()
            } else {
                (g - w).abs()
            }
        })
        .fold(0.0_f64, f64::max)
}

/// Max over h of max(|dlower|,|dupper|) / R half-width (interval metric per
/// the interfaces).
fn max_interval_dev(lower: &[f64], upper: &[f64], r_lower: &[f64], r_upper: &[f64]) -> f64 {
    assert_eq!(lower.len(), r_lower.len());
    assert_eq!(upper.len(), r_upper.len());
    let mut worst = 0.0_f64;
    for i in 0..lower.len() {
        let half_width = (r_upper[i] - r_lower[i]) / 2.0;
        let dl = (lower[i] - r_lower[i]).abs();
        let du = (upper[i] - r_upper[i]).abs();
        let dev = if half_width > 0.0 {
            dl.max(du) / half_width
        } else {
            dl.max(du)
        };
        worst = worst.max(dev);
    }
    worst
}

const H: usize = 12;
const LEVEL: f64 = 0.95;

// ─── E5: crate at R's fitted parameters (ETS family) ────────────────────────

struct E5Result {
    loglik_rel_dev: f64,
    point_rel_dev: f64,
    interval_dev: f64,
}

#[allow(clippy::too_many_arguments)]
fn run_e5(
    spec: ETSSpec,
    period: usize,
    multiplicative_seasonal: bool,
    values: &[f64],
    fit: &Value,
    r_point: &[f64],
    r_lower: &[f64],
    r_upper: &[f64],
) -> E5Result {
    let par = &fit["par"];
    let alpha = par_num(par, "alpha");
    let beta = par_opt(par, "beta");
    let gamma = par_opt(par, "gamma");
    let phi = par_opt(par, "phi");
    let level0 = par_num(par, "l");
    let trend0 = par_opt(par, "b").unwrap_or(0.0);
    let seasonals = if spec.has_seasonal() {
        seasonal_states_from_par(par, period, multiplicative_seasonal)
    } else {
        vec![]
    };

    let mut model = ETSModel::with_params_and_states(
        spec,
        period.max(1),
        alpha,
        beta,
        gamma,
        phi,
        level0,
        trend0,
        seasonals,
    );
    let ts = ts_synthetic(values);
    model.fit(&ts).expect("with_params_and_states fit");

    let r_loglik = common::num(&fit["loglik"]);
    let crate_loglik = model.log_likelihood().expect("log_likelihood available");
    let loglik_rel_dev = if r_loglik.abs() > 0.0 {
        (crate_loglik - r_loglik).abs() / r_loglik.abs()
    } else {
        (crate_loglik - r_loglik).abs()
    };

    let forecast = model.predict_with_intervals(H, LEVEL).expect("predict");
    let point = forecast.primary().to_vec();
    let point_rel_dev = max_rel_dev(&point, r_point);

    let interval_dev = match forecast.lower().and_then(|l| l.first()).cloned() {
        Some(lower) => {
            let upper = forecast.upper().and_then(|u| u.first()).cloned().unwrap();
            max_interval_dev(&lower, &upper, r_lower, r_upper)
        }
        None => f64::NAN,
    };

    E5Result {
        loglik_rel_dev,
        point_rel_dev,
        interval_dev,
    }
}

// ─── E6: crate's own fit (its own optimizer) ────────────────────────────────

struct E6Result {
    crate_loglik: f64,
    r_loglik: f64,
    alpha: Option<f64>,
    beta: Option<f64>,
    gamma: Option<f64>,
    phi: Option<f64>,
}

/// R's documented `ets()` bounds (interfaces "Prior upstream evidence"):
/// alpha/beta/gamma in \[1e-4, 0.9999\], beta <= alpha, gamma <= 1 - alpha,
/// phi in \[0.8, 0.98\].
fn within_r_bounds(e6: &E6Result) -> bool {
    let in_unit = |v: f64| (1e-4..=0.9999).contains(&v);
    let alpha_ok = e6.alpha.map(in_unit).unwrap_or(true);
    let beta_ok = e6.beta.map(in_unit).unwrap_or(true);
    let gamma_ok = e6.gamma.map(in_unit).unwrap_or(true);
    let phi_ok = e6.phi.map(|p| (0.8..=0.98).contains(&p)).unwrap_or(true);
    let beta_le_alpha = match (e6.alpha, e6.beta) {
        (Some(a), Some(b)) => b <= a + 1e-9,
        _ => true,
    };
    let gamma_le = match (e6.alpha, e6.gamma) {
        (Some(a), Some(g)) => g <= 1.0 - a + 1e-9,
        _ => true,
    };
    alpha_ok && beta_ok && gamma_ok && phi_ok && beta_le_alpha && gamma_le
}

fn run_e6_ets(spec: ETSSpec, period: usize, values: &[f64], r_loglik: f64) -> E6Result {
    let mut model = ETSModel::new(spec, period.max(1));
    let ts = ts_synthetic(values);
    model.fit(&ts).expect("ETS::new fit");
    let crate_loglik = model.log_likelihood().expect("log_likelihood available");
    E6Result {
        crate_loglik,
        r_loglik,
        alpha: model.alpha(),
        beta: model.beta(),
        gamma: model.gamma(),
        phi: model.phi(),
    }
}

fn print_e6(label: &str, e6: &E6Result) {
    let gap = e6.crate_loglik - e6.r_loglik;
    let location = if e6.crate_loglik >= e6.r_loglik {
        if within_r_bounds(e6) {
            "reference (R optimiser stopped at an inferior, in-bounds optimum)"
        } else {
            "crate (crate params outside R's documented bounds)"
        }
    } else {
        "crate (crate optimiser misses an optimum its own objective scores higher)"
    };
    println!(
        "  E6 {label}: crate_loglik={:.6} r_loglik={:.6} gap={:.6} alpha={:?} beta={:?} gamma={:?} phi={:?} in_r_bounds={} -> {location}",
        e6.crate_loglik, e6.r_loglik, gap, e6.alpha, e6.beta, e6.gamma, e6.phi, within_r_bounds(e6)
    );
}

// ─── per-check diagnosis functions ──────────────────────────────────────────

fn r_point(block: &Value) -> Vec<f64> {
    common::f64_vec(&block["point"])
}
fn r_lower95(block: &Value) -> Vec<f64> {
    common::f64_vec(&block["lower_95"])
}
fn r_upper95(block: &Value) -> Vec<f64> {
    common::f64_vec(&block["upper_95"])
}

/// E2: forecast() wrapper output must be bit-identical to the same model
/// built directly from anofox-forecast the way the wrapper builds it.
#[allow(clippy::too_many_arguments)]
fn assert_wrapper_equals_direct(
    model: ModelType,
    ets_spec: Option<&str>,
    seasonal_period: usize,
    values: &[f64],
    direct_point: &[f64],
    direct_lower: &[f64],
    direct_upper: &[f64],
    ctx: &str,
) {
    let opts: Vec<Option<f64>> = values.iter().map(|v| Some(*v)).collect();
    let options = ForecastOptions {
        model,
        ets_spec: ets_spec.map(|s| s.to_string()),
        horizon: H,
        confidence_level: LEVEL,
        seasonal_period,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let out = forecast(&opts, &options).expect("forecast() must succeed");
    assert_eq!(
        out.point, direct_point,
        "{ctx}: forecast() point differs from direct crate construction (E2 extension-side divergence)"
    );
    assert_eq!(
        out.lower, direct_lower,
        "{ctx}: forecast() lower differs from direct crate construction (E2 extension-side divergence)"
    );
    assert_eq!(
        out.upper, direct_upper,
        "{ctx}: forecast() upper differs from direct crate construction (E2 extension-side divergence)"
    );
}

fn direct_holt(values: &[f64]) -> (Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut m = HoltLinearTrend::auto();
    m.fit(&ts_synthetic(values)).expect("Holt fit");
    let f = m.predict_with_intervals(H, LEVEL).expect("predict");
    let lower = f
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .unwrap_or_default();
    let upper = f
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .unwrap_or_default();
    (f.primary().to_vec(), lower, upper)
}

fn direct_holt_winters(values: &[f64], period: usize) -> (Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut m = HoltWintersModel::auto(period, SeasonalType::Additive);
    m.fit(&ts_synthetic(values)).expect("HoltWinters fit");
    let f = m.predict_with_intervals(H, LEVEL).expect("predict");
    let lower = f
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .unwrap_or_default();
    let upper = f
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .unwrap_or_default();
    (f.primary().to_vec(), lower, upper)
}

fn direct_ets(values: &[f64], spec: ETSSpec, period: usize) -> (Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut m = ETSModel::new(spec, period.max(1));
    m.fit(&ts_synthetic(values)).expect("ETS fit");
    let f = m.predict_with_intervals(H, LEVEL).expect("predict");
    let lower = f
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .unwrap_or_default();
    let upper = f
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .unwrap_or_default();
    (f.primary().to_vec(), lower, upper)
}

#[allow(clippy::type_complexity)]
fn direct_auto_ets(
    values: &[f64],
    period: usize,
    pool: Option<ModelPool>,
) -> (
    Vec<f64>,
    Vec<f64>,
    Vec<f64>,
    Option<ETSSpec>,
    Vec<(ETSSpec, f64)>,
) {
    let mut cfg = if period > 1 {
        AutoETSConfig::with_period(period)
    } else {
        AutoETSConfig::non_seasonal()
    };
    if let Some(p) = pool {
        cfg = cfg.with_model_pool(p);
    }
    let mut m = AutoETS::with_config(cfg);
    m.fit(&ts_synthetic(values)).expect("AutoETS fit");
    let spec = m.selected_spec();
    let scores = m.model_scores().to_vec();
    let f = m.predict_with_intervals(H, LEVEL).expect("predict");
    let lower = f
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .unwrap_or_default();
    let upper = f
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .unwrap_or_default();
    (f.primary().to_vec(), lower, upper, spec, scores)
}

fn direct_ses_optimized(values: &[f64]) -> (Vec<f64>, Vec<f64>, Vec<f64>) {
    let mut m = SimpleExponentialSmoothing::auto();
    m.fit(&ts_synthetic(values)).expect("SESOptimized fit");
    let f = m.predict_with_intervals(H, LEVEL).expect("predict");
    let lower = f
        .lower()
        .and_then(|l| l.first())
        .cloned()
        .unwrap_or_default();
    let upper = f
        .upper()
        .and_then(|u| u.first())
        .cloned()
        .unwrap_or_default();
    (f.primary().to_vec(), lower, upper)
}

// ══════════════════════════════════════════════════════════════════════════
// Test 1: wrapper_equals_direct_crate_construction (E2, permanent, asserting)
// ══════════════════════════════════════════════════════════════════════════

#[test]
fn wrapper_equals_direct_crate_construction() {
    let air = air_values();
    let rw = rw_values();
    let inter = inter_values();

    // air/holt -> Holt (D-12/D-13, 17-03 Task 1: SQL 'Holt' is remapped to
    // the crate's ETS(A,A,N), R's holt() definition — see
    // forecast.rs::forecast_holt_lib's doc comment and RC-01).
    {
        let (p, l, u) = direct_ets(&air, ETSSpec::aan(), 1);
        assert_wrapper_equals_direct(ModelType::Holt, None, 0, &air, &p, &l, &u, "air/holt/Holt");
    }
    // air/hw_additive -> HoltWinters
    {
        let (p, l, u) = direct_holt_winters(&air, 12);
        assert_wrapper_equals_direct(
            ModelType::HoltWinters,
            None,
            12,
            &air,
            &p,
            &l,
            &u,
            "air/hw_additive/HoltWinters",
        );
    }
    // air/hw_additive -> ETS:AAA (explicit spec)
    {
        let (p, l, u) = direct_ets(&air, ETSSpec::aaa(), 12);
        assert_wrapper_equals_direct(
            ModelType::ETS,
            Some("AAA"),
            12,
            &air,
            &p,
            &l,
            &u,
            "air/hw_additive/ETS:AAA",
        );
    }
    // air/hw_multiplicative -> ETS:MAM
    {
        let (p, l, u) = direct_ets(&air, ETSSpec::mam(), 12);
        assert_wrapper_equals_direct(
            ModelType::ETS,
            Some("MAM"),
            12,
            &air,
            &p,
            &l,
            &u,
            "air/hw_multiplicative/ETS:MAM",
        );
    }
    // air/ets_MAM -> ETS:MAM (same construction, different R comparison —
    // the forecast() call itself is identical to the block above)
    // air/ets_AAA -> ETS:AAA (same construction, different R comparison)
    // air/ets_auto -> AutoETS
    {
        let (p, l, u, _, _) = direct_auto_ets(&air, 12, None);
        assert_wrapper_equals_direct(
            ModelType::AutoETS,
            None,
            12,
            &air,
            &p,
            &l,
            &u,
            "air/ets_auto/AutoETS",
        );
    }
    // rw/holt -> Holt (D-12/D-13: same remap as air/holt/Holt above)
    {
        let (p, l, u) = direct_ets(&rw, ETSSpec::aan(), 1);
        assert_wrapper_equals_direct(ModelType::Holt, None, 0, &rw, &p, &l, &u, "rw/holt/Holt");
    }
    // rw/holt -> ETS:AAN
    {
        let (p, l, u) = direct_ets(&rw, ETSSpec::aan(), 1);
        assert_wrapper_equals_direct(
            ModelType::ETS,
            Some("AAN"),
            0,
            &rw,
            &p,
            &l,
            &u,
            "rw/holt/ETS:AAN",
        );
    }
    // air/holt -> ETS:AAN
    {
        let (p, l, u) = direct_ets(&air, ETSSpec::aan(), 1);
        assert_wrapper_equals_direct(
            ModelType::ETS,
            Some("AAN"),
            0,
            &air,
            &p,
            &l,
            &u,
            "air/holt/ETS:AAN",
        );
    }
    // inter/ses -> SESOptimized
    {
        let (p, l, u) = direct_ses_optimized(&inter);
        assert_wrapper_equals_direct(
            ModelType::SESOptimized,
            None,
            0,
            &inter,
            &p,
            &l,
            &u,
            "inter/ses/SESOptimized",
        );
    }
    // trend/holt -> Holt (D-12/D-13, 17-03 Task 2: same remap as air/rw above)
    {
        let trend = trend_values();
        let (p, l, u) = direct_ets(&trend, ETSSpec::aan(), 1);
        assert_wrapper_equals_direct(
            ModelType::Holt,
            None,
            0,
            &trend,
            &p,
            &l,
            &u,
            "trend/holt/Holt",
        );
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Test 2: diagnosis_report (E1-E9 ladder, prints harness-output.txt content)
// ══════════════════════════════════════════════════════════════════════════

#[test]
fn diagnosis_report() {
    let air_fx = air_fixture();
    let sim_fx = sim_fixture();
    let air = air_values();
    let rw = rw_values();
    let inter = inter_values();
    let trend = trend_values();

    let mut rows = 0usize;

    // ---- air/holt/ETS:AAN ----
    {
        let fit = &air_fx["models"]["holt"]["fit"];
        let block = &air_fx["models"]["holt"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::aan(), 1, false, &air, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::aan(), 1, &air, common::num(&fit["loglik"]));
        println!("=== air/holt/ETS:AAN ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("air/holt/ETS:AAN", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e5.point_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- air/holt/Holt (E4: mapping candidate ETS:AAN vs HoltLinearTrend) ----
    {
        let block = &air_fx["models"]["holt"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let (hp, hl, hu) = direct_holt(&air);
        let holt_point_dev = max_rel_dev(&hp, &r_pt);
        let holt_interval_dev = max_interval_dev(&hl, &hu, &r_lo, &r_up);
        let (ep, el, eu) = direct_ets(&air, ETSSpec::aan(), 1);
        let ets_point_dev = max_rel_dev(&ep, &r_pt);
        let ets_interval_dev = max_interval_dev(&el, &eu, &r_lo, &r_up);
        println!("=== air/holt/Holt (E4 mapping check) ===");
        println!(
            "  HoltLinearTrend: point_dev={:.3e} interval_dev={:.3e}",
            holt_point_dev, holt_interval_dev
        );
        println!(
            "  ETS(A,A,N) R-faithful: point_dev={:.3e} interval_dev={:.3e} cut={:.2}x",
            ets_point_dev,
            ets_interval_dev,
            holt_point_dev / ets_point_dev.max(1e-300)
        );
        assert!(holt_point_dev.is_finite());
        assert!(ets_point_dev.is_finite());
        rows += 1;
    }

    // ---- air/hw_additive/HoltWinters (E4 mapping check) ----
    {
        let block = &air_fx["models"]["hw_additive"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let (hp, hl, hu) = direct_holt_winters(&air, 12);
        let hw_point_dev = max_rel_dev(&hp, &r_pt);
        let hw_interval_dev = max_interval_dev(&hl, &hu, &r_lo, &r_up);
        let (ep, el, eu) = direct_ets(&air, ETSSpec::aaa(), 12);
        let ets_point_dev = max_rel_dev(&ep, &r_pt);
        let ets_interval_dev = max_interval_dev(&el, &eu, &r_lo, &r_up);
        println!("=== air/hw_additive/HoltWinters (E4 mapping check) ===");
        println!(
            "  HoltWinters: point_dev={:.3e} interval_dev={:.3e}",
            hw_point_dev, hw_interval_dev
        );
        println!(
            "  ETS(A,A,A) R-faithful: point_dev={:.3e} interval_dev={:.3e} cut={:.2}x",
            ets_point_dev,
            ets_interval_dev,
            hw_point_dev / ets_point_dev.max(1e-300)
        );
        assert!(hw_point_dev.is_finite());
        assert!(ets_point_dev.is_finite());
        rows += 1;
    }

    // ---- air/hw_additive/ETS:AAA (E5/E6) ----
    {
        let fit = &air_fx["models"]["hw_additive"]["fit"];
        let block = &air_fx["models"]["hw_additive"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::aaa(), 12, false, &air, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::aaa(), 12, &air, common::num(&fit["loglik"]));
        println!("=== air/hw_additive/ETS:AAA ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("air/hw_additive/ETS:AAA", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- air/hw_multiplicative/ETS:MAM (E5/E6; R ref used hw()'s opt.crit="mse") ----
    {
        let fit = &air_fx["models"]["hw_multiplicative"]["fit"];
        let block = &air_fx["models"]["hw_multiplicative"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::mam(), 12, true, &air, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::mam(), 12, &air, common::num(&fit["loglik"]));
        println!("=== air/hw_multiplicative/ETS:MAM ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("air/hw_multiplicative/ETS:MAM", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- air/ets_auto/AutoETS (E6/E7) ----
    {
        let fit = &air_fx["models"]["ets_auto"]["fit"];
        let diag = &air_fx["diagnosis"];
        let r_method = fit["method"].as_str().unwrap();
        // R-selected spec letters, e.g. "ETS(M,Ad,M)" -> "MAdM"
        let r_spec_letters = r_method
            .trim_start_matches("ETS(")
            .trim_end_matches(')')
            .replace(',', "");
        let r_aicc = common::num(&fit["aicc"]);
        let (_p, _l, _u, crate_spec, crate_scores) = direct_auto_ets(&air, 12, None);
        println!("=== air/ets_auto/AutoETS ===");
        println!("  R selected: {} (AICc {:.4})", r_spec_letters, r_aicc);
        println!("  crate selected: {:?}", crate_spec.map(|s| s.short_name()));
        if let Some(candidates) = diag.get("ets_candidates").and_then(|c| c.as_array()) {
            for cand in candidates {
                let spec_str = cand["spec"].as_str().unwrap_or("");
                if spec_str.is_empty() {
                    continue;
                }
                if let Some(fitted) = cand.get("fitted").and_then(|v| v.as_bool()) {
                    if !fitted {
                        continue;
                    }
                }
                let r_cand_aicc = cand.get("aicc").map(common::num);
                let crate_cand_score = crate_scores.iter().find(|(spec, _)| {
                    spec.short_name()
                        == format!(
                            "ETS({},{},{})",
                            &spec_str[0..1],
                            &spec_str[1..spec_str.len() - 1],
                            &spec_str[spec_str.len() - 1..]
                        )
                });
                if let (Some(r_a), Some((_, crate_a))) = (r_cand_aicc, crate_cand_score) {
                    println!(
                        "  E7 candidate {spec_str}: r_aicc={:.4} crate_aicc={:.4} diff={:.4}",
                        r_a,
                        crate_a,
                        crate_a - r_a
                    );
                }
            }
        }
        // E6 at R's selected spec (M,Ad,M) specifically: is the crate's own
        // optimizer underperforming R on the spec R actually picked, which
        // would explain why AutoETS doesn't select it?
        let e6_madm = run_e6_ets(ETSSpec::madm(), 12, &air, common::num(&fit["loglik"]));
        print_e6("air/ets_auto/AutoETS (spec M,Ad,M, R's pick)", &e6_madm);
        assert!(r_aicc.is_finite());
        rows += 1;
    }

    // ---- air/ets_MAM/ETS:MAM (E5/E6, apples-to-apples with crate's own criterion) ----
    {
        let fit = &air_fx["models"]["ets_MAM"]["fit"];
        let block = &air_fx["models"]["ets_MAM"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::mam(), 12, true, &air, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::mam(), 12, &air, common::num(&fit["loglik"]));
        println!("=== air/ets_MAM/ETS:MAM ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("air/ets_MAM/ETS:MAM", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- air/ets_AAA/ETS:AAA (E5/E6) ----
    {
        let fit = &air_fx["models"]["ets_AAA"]["fit"];
        let block = &air_fx["models"]["ets_AAA"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::aaa(), 12, false, &air, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::aaa(), 12, &air, common::num(&fit["loglik"]));
        println!("=== air/ets_AAA/ETS:AAA ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("air/ets_AAA/ETS:AAA", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- rw/holt/ETS:AAN ----
    {
        let fit = &sim_fx["series"]["rw"]["models"]["holt"]["fit"];
        let block = &sim_fx["series"]["rw"]["models"]["holt"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let e5 = run_e5(ETSSpec::aan(), 1, false, &rw, fit, &r_pt, &r_lo, &r_up);
        let e6 = run_e6_ets(ETSSpec::aan(), 1, &rw, common::num(&fit["loglik"]));
        println!("=== rw/holt/ETS:AAN ===");
        println!(
            "  E5: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        print_e6("rw/holt/ETS:AAN", &e6);
        assert!(e5.loglik_rel_dev.is_finite());
        assert!(e6.crate_loglik.is_finite());
        rows += 1;
    }

    // ---- rw/holt/Holt (E4 mapping check) ----
    {
        let block = &sim_fx["series"]["rw"]["models"]["holt"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let (hp, hl, hu) = direct_holt(&rw);
        let holt_point_dev = max_rel_dev(&hp, &r_pt);
        let holt_interval_dev = max_interval_dev(&hl, &hu, &r_lo, &r_up);
        let (ep, el, eu) = direct_ets(&rw, ETSSpec::aan(), 1);
        let ets_point_dev = max_rel_dev(&ep, &r_pt);
        let ets_interval_dev = max_interval_dev(&el, &eu, &r_lo, &r_up);
        println!("=== rw/holt/Holt (E4 mapping check) ===");
        println!(
            "  HoltLinearTrend: point_dev={:.3e} interval_dev={:.3e}",
            holt_point_dev, holt_interval_dev
        );
        println!(
            "  ETS(A,A,N) R-faithful: point_dev={:.3e} interval_dev={:.3e} cut={:.2}x",
            ets_point_dev,
            ets_interval_dev,
            holt_point_dev / ets_point_dev.max(1e-300)
        );
        assert!(holt_point_dev.is_finite());
        assert!(ets_point_dev.is_finite());
        rows += 1;
    }

    // ---- inter/ses/SESOptimized (E4/E9 — fixed l0 vs R's jointly-optimised l0) ----
    {
        let block = &sim_fx["series"]["inter"]["models"]["ses"];
        let fit = &block["fit"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let (sp, sl, su) = direct_ses_optimized(&inter);
        let ses_point_dev = max_rel_dev(&sp, &r_pt);
        let ses_interval_dev = max_interval_dev(&sl, &su, &r_lo, &r_up);
        let (ep, el, eu) = direct_ets(&inter, ETSSpec::ann(), 1);
        let ets_point_dev = max_rel_dev(&ep, &r_pt);
        let ets_interval_dev = max_interval_dev(&el, &eu, &r_lo, &r_up);
        let e5 = run_e5(ETSSpec::ann(), 1, false, &inter, fit, &r_pt, &r_lo, &r_up);
        println!("=== inter/ses/SESOptimized (E4/E5/E9) ===");
        println!(
            "  SimpleExponentialSmoothing::auto(): point_dev={:.3e} interval_dev={:.3e}",
            ses_point_dev, ses_interval_dev
        );
        println!(
            "  ETS(A,N,N) R-faithful mapping: point_dev={:.3e} interval_dev={:.3e} cut={:.2}x",
            ets_point_dev,
            ets_interval_dev,
            ses_point_dev / ets_point_dev.max(1e-300)
        );
        println!(
            "  E5 ETS(A,N,N) at R's params: loglik_rel_dev={:.3e} point_rel_dev={:.3e} interval_dev={:.3e}",
            e5.loglik_rel_dev, e5.point_rel_dev, e5.interval_dev
        );
        let e6 = run_e6_ets(ETSSpec::ann(), 1, &inter, common::num(&fit["loglik"]));
        print_e6("inter/ses/ETS:ANN", &e6);
        {
            let mut ses_model = SimpleExponentialSmoothing::auto();
            ses_model.fit(&ts_synthetic(&inter)).expect("SES fit");
            println!(
                "  SES own fit: alpha={:?} l0_fixed_to_values[0]={}",
                ses_model.alpha(),
                inter[0]
            );
        }
        println!(
            "  R ses(inter) par: alpha={} l0={}",
            par_num(&fit["par"], "alpha"),
            par_num(&fit["par"], "l")
        );
        assert!(ses_point_dev.is_finite());
        assert!(ets_point_dev.is_finite());
        assert!(e5.loglik_rel_dev.is_finite());
        rows += 1;
    }

    // ---- trend/holt/Holt (E4 mapping check, 17-03 Task 2) ----
    {
        let block = &sim_fx["series"]["trend"]["models"]["holt"];
        let r_pt = r_point(block);
        let r_lo = r_lower95(block);
        let r_up = r_upper95(block);
        let (hp, hl, hu) = direct_holt(&trend);
        let holt_point_dev = max_rel_dev(&hp, &r_pt);
        let holt_interval_dev = max_interval_dev(&hl, &hu, &r_lo, &r_up);
        let (ep, el, eu) = direct_ets(&trend, ETSSpec::aan(), 1);
        let ets_point_dev = max_rel_dev(&ep, &r_pt);
        let ets_interval_dev = max_interval_dev(&el, &eu, &r_lo, &r_up);
        println!("=== trend/holt/Holt (E4 mapping check) ===");
        println!(
            "  HoltLinearTrend: point_dev={:.3e} interval_dev={:.3e}",
            holt_point_dev, holt_interval_dev
        );
        println!(
            "  ETS(A,A,N) R-faithful: point_dev={:.3e} interval_dev={:.3e} cut={:.2}x",
            ets_point_dev,
            ets_interval_dev,
            holt_point_dev / ets_point_dev.max(1e-300)
        );
        assert!(holt_point_dev.is_finite());
        assert!(ets_point_dev.is_finite());
        rows += 1;
    }

    println!("diagnosis rows: {rows}");
    assert_eq!(
        rows, 12,
        "17-03 Task 2 diagnosis scope must cover exactly 12 checks (11 from Task 1 + trend/holt/Holt)"
    );
}

// ══════════════════════════════════════════════════════════════════════════
// Test 3: autoarima_report (D-15)
// ══════════════════════════════════════════════════════════════════════════

#[allow(clippy::type_complexity)]
fn parse_autoarima_order(
    model_name: &str,
) -> Option<(usize, usize, usize, Option<(usize, usize, usize, usize)>)> {
    let rest = model_name.strip_prefix("AutoARIMA(")?;
    let close = rest.find(')')?;
    let non_seasonal = &rest[..close];
    let parts: Vec<usize> = non_seasonal
        .split(',')
        .map(|s| s.trim().parse().ok())
        .collect::<Option<Vec<_>>>()?;
    if parts.len() != 3 {
        return None;
    }
    let (p, d, q) = (parts[0], parts[1], parts[2]);
    let after = &rest[close + 1..];
    if let Some(seasonal_rest) = after.strip_prefix('(') {
        let close2 = seasonal_rest.find(')')?;
        let seasonal_part = &seasonal_rest[..close2];
        let sparts: Vec<usize> = seasonal_part
            .split(',')
            .map(|s| s.trim().parse().ok())
            .collect::<Option<Vec<_>>>()?;
        if sparts.len() != 3 {
            return None;
        }
        let bracket_rest = &seasonal_rest[close2 + 1..];
        let s_str = bracket_rest.trim_start_matches('[').trim_end_matches(']');
        let s: usize = s_str.trim().parse().ok()?;
        Some((p, d, q, Some((sparts[0], sparts[1], sparts[2], s))))
    } else {
        Some((p, d, q, None))
    }
}

#[test]
fn autoarima_report() {
    let air_fx = air_fixture();
    let sim_fx = sim_fixture();
    let air = air_values();
    let rw = rw_values();
    let trend = trend_values();

    let mut rows = 0usize;

    {
        let block = &air_fx["models"]["auto_arima"];
        let order = &block["fit"]["order"];
        run_autoarima_block("air", &air, order, 12, block);
        rows += 1;
    }
    {
        let block = &sim_fx["series"]["rw"]["models"]["auto_arima"];
        let order = &block["fit"]["order"];
        run_autoarima_block("rw", &rw, order, 0, block);
        rows += 1;
    }
    {
        let block = &sim_fx["series"]["trend"]["models"]["auto_arima"];
        let order = &block["fit"]["order"];
        run_autoarima_block("trend", &trend, order, 0, block);
        rows += 1;
    }

    println!("autoarima rows: {rows}");
    assert_eq!(
        rows, 3,
        "17-03 Task 2 autoarima scope must cover exactly 3 series"
    );
}

fn run_autoarima_block(
    series: &str,
    values: &[f64],
    r_order: &Value,
    seasonal_period: usize,
    block: &Value,
) {
    let opts: Vec<Option<f64>> = values.iter().map(|v| Some(*v)).collect();
    let options = ForecastOptions {
        model: ModelType::AutoARIMA,
        horizon: H,
        confidence_level: LEVEL,
        seasonal_period,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let out = forecast(&opts, &options).expect("AutoARIMA forecast() must succeed");
    let parsed = parse_autoarima_order(&out.model_name);

    let r_p = r_order["p"].as_u64().unwrap() as usize;
    let r_d = r_order["d"].as_u64().unwrap() as usize;
    let r_q = r_order["q"].as_u64().unwrap() as usize;
    let r_seasonal = r_order.get("P").map(|_| {
        (
            r_order["P"].as_u64().unwrap() as usize,
            r_order["D"].as_u64().unwrap() as usize,
            r_order["Q"].as_u64().unwrap() as usize,
            r_order["Frequency"].as_u64().unwrap() as usize,
        )
    });

    let order_match = match (&parsed, &r_seasonal) {
        (Some((p, d, q, Some((cp, cd, cq, s)))), Some((rp, rd, rq, rs))) => {
            *p == r_p
                && *d == r_d
                && *q == r_q
                && *cp == *rp
                && *cd == *rd
                && *cq == *rq
                && *s == *rs
        }
        (Some((p, d, q, None)), None) => *p == r_p && *d == r_d && *q == r_q,
        _ => false,
    };

    let r_pt = r_point(block);
    let r_lo = r_lower95(block);
    let r_up = r_upper95(block);
    let point_dev = max_rel_dev(&out.point, &r_pt);
    let interval_dev = if out.lower.len() == r_lo.len() && !out.lower.is_empty() {
        max_interval_dev(&out.lower, &out.upper, &r_lo, &r_up)
    } else {
        f64::NAN
    };
    let tier = if point_dev <= 1e-3 && interval_dev <= 1e-2 {
        "parity"
    } else {
        "outside"
    };

    println!(
        "autoarima {series}: r_order=({r_p},{r_d},{r_q}{}) crate={} order_match={order_match} point={point_dev:.6e} interval={interval_dev:.6e} tier={tier}",
        r_seasonal
            .map(|(cp, cd, cq, s)| format!(")({cp},{cd},{cq})[{s}"))
            .unwrap_or_default(),
        out.model_name
    );
}

// Keep trend_values()/ts_real_monthly() referenced so Task 2 can extend this
// scope without an unused-function warning in the interim.
#[allow(dead_code)]
fn _touch() {
    let _ = trend_values();
    let _ = ts_real_monthly(&[1.0, 2.0, 3.0], 2000, 1);
}

// ══════════════════════════════════════════════════════════════════════════
// D-15 Miss diagnosis: air/auto_arima interval tier=outside (E8)
// ══════════════════════════════════════════════════════════════════════════

#[test]
fn autoarima_air_interval_e8_probe() {
    let air_fx = air_fixture();
    let air = air_values();
    let block = &air_fx["models"]["auto_arima"];
    let r_pt = r_point(block);
    let r_lo = r_lower95(block);
    let r_up = r_upper95(block);

    // AutoARIMA (selection path)
    let opts: Vec<Option<f64>> = air.iter().map(|v| Some(*v)).collect();
    let auto_options = ForecastOptions {
        model: ModelType::AutoARIMA,
        horizon: H,
        confidence_level: LEVEL,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let auto_out = forecast(&opts, &auto_options).expect("AutoARIMA");

    // Explicit ARIMA at R's exact selected order (E8: isolates selection vs
    // estimation — if this matches AutoARIMA's own output, the crate's
    // selection and estimation paths agree internally).
    let explicit_options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: H,
        confidence_level: LEVEL,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        arima_order: Some([2, 1, 1]),
        seasonal_order: Some([0, 1, 0]),
        ..Default::default()
    };
    let explicit_out = forecast(&opts, &explicit_options).expect("ARIMA(2,1,1)(0,1,0)[12]");

    println!("=== D-15 Miss E8: air/auto_arima interval ===");
    println!(
        "  AutoARIMA == explicit ARIMA at same order? point {} lower {} upper {}",
        auto_out.point == explicit_out.point,
        auto_out.lower == explicit_out.lower,
        auto_out.upper == explicit_out.upper
    );
    for h in 0..H {
        let r_half = (r_up[h] - r_lo[h]) / 2.0;
        let c_half = (auto_out.upper[h] - auto_out.lower[h]) / 2.0;
        println!(
            "  h={:>2} r_point={:>9.3} c_point={:>9.3} r_half={:>8.4} c_half={:>8.4} ratio(c/r)={:.4}",
            h + 1,
            r_pt[h],
            auto_out.point[h],
            r_half,
            c_half,
            c_half / r_half
        );
    }
}

#[test]
fn autoarima_air_explicit_arima_e8_probe() {
    let air_fx = air_fixture();
    let air = air_values();
    let block = &air_fx["models"]["auto_arima"];
    let r_pt = r_point(block);
    let r_lo = r_lower95(block);
    let r_up = r_upper95(block);

    let opts: Vec<Option<f64>> = air.iter().map(|v| Some(*v)).collect();
    let explicit_options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: H,
        confidence_level: LEVEL,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        arima_order: Some([2, 1, 1]),
        seasonal_order: Some([0, 1, 0]),
        ..Default::default()
    };
    let explicit_out = forecast(&opts, &explicit_options).expect("ARIMA(2,1,1)(0,1,0)[12]");
    let point_dev = max_rel_dev(&explicit_out.point, &r_pt);
    let interval_dev = max_interval_dev(&explicit_out.lower, &explicit_out.upper, &r_lo, &r_up);
    println!(
        "explicit ARIMA(2,1,1)(0,1,0)[12] on air: model_name={} point_dev={:.6e} interval_dev={:.6e}",
        explicit_out.model_name, point_dev, interval_dev
    );
    for h in 0..H {
        let r_half = (r_up[h] - r_lo[h]) / 2.0;
        let c_half = (explicit_out.upper[h] - explicit_out.lower[h]) / 2.0;
        println!(
            "  h={:>2} r_half={:>8.4} c_half={:>8.4} ratio(c/r)={:.4}",
            h + 1,
            r_half,
            c_half,
            c_half / r_half
        );
    }
}

#[test]
fn autoarima_air_e3_timestamp_probe() {
    use anofox_forecast::models::arima::SARIMA;
    let air_fx = air_fixture();
    let air = air_values();
    let block = &air_fx["models"]["auto_arima"];
    let r_lo = r_lower95(block);
    let r_up = r_upper95(block);

    let mut m_synth = SARIMA::new(2, 1, 1, 0, 1, 0, 12);
    m_synth
        .fit(&ts_synthetic(&air))
        .expect("SARIMA fit (synthetic ts)");
    let f_synth = m_synth.predict_with_intervals(H, LEVEL).expect("predict");
    let lo_synth = f_synth.lower().and_then(|l| l.first()).cloned().unwrap();
    let up_synth = f_synth.upper().and_then(|u| u.first()).cloned().unwrap();

    let mut m_real = SARIMA::new(2, 1, 1, 0, 1, 0, 12);
    m_real
        .fit(&ts_real_monthly(&air, 1949, 1))
        .expect("SARIMA fit (real monthly ts)");
    let f_real = m_real.predict_with_intervals(H, LEVEL).expect("predict");
    let lo_real = f_real.lower().and_then(|l| l.first()).cloned().unwrap();
    let up_real = f_real.upper().and_then(|u| u.first()).cloned().unwrap();

    let dev_synth = max_interval_dev(&lo_synth, &up_synth, &r_lo, &r_up);
    let dev_real = max_interval_dev(&lo_real, &up_real, &r_lo, &r_up);
    println!(
        "E3 timestamp sensitivity: interval_dev synthetic_hourly={:.6e} real_monthly={:.6e} same_bits={}",
        dev_synth, dev_real, lo_synth == lo_real && up_synth == up_real
    );
}

// ══════════════════════════════════════════════════════════════════════════
// Upstream issue reproductions (RC-03, RC-04, RC-05). RC-03/RC-04 bodies are
// mirrored verbatim (modulo the fn signature) in their matching drafts under
// diagnosis/upstream-issues/ — exact minimal reproductions, compiled and run
// here against anofox-forecast 0.16.2 (RC-03/RC-04 themselves unaffected by
// the 0.16.2 bump). #[ignore]d because they assert the UPSTREAM discrepancy
// and are expected to start failing once the crate fixes it (a signal to
// tighten the corresponding tolerance). RC-05 is RESOLVED as of 0.16.2
// (upstream PR #230/#229, 18-05 gap closure): its repro is inverted to
// assert the fix holds and is no longer #[ignore]d — see the comment on
// repro_rc_05 below.
// ══════════════════════════════════════════════════════════════════════════

#[test]
#[ignore = "upstream repro RC-03"]
fn repro_rc_03() {
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

#[test]
#[ignore = "upstream repro RC-04"]
fn repro_rc_04() {
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

/// RC-05 RESOLVED (18-05 gap closure, anofox-forecast 0.16.2, upstream PR
/// #230/#229): this test used to assert the crate's SARIMA interval was
/// systematically narrower than R's KalmanForecast variance
/// (worst_ratio < 0.90, measured 0.818 on 0.16.1). The upstream fix brings
/// the worst-horizon ratio to 0.9951 on 0.16.2. Inverted (not deleted) to
/// assert the fix holds, so a future crate bump regressing this exact
/// defect fails loudly instead of silently reappearing. No longer
/// `#[ignore]` -- it runs as part of the normal suite now that the gap is
/// closed.
#[test]
fn repro_rc_05() {
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
        worst_ratio >= 0.95,
        "RC-05 regression: expected the crate's SARIMA interval to stay at parity with R's \
         stats::arima KalmanForecast variance (ratio >= 0.95, anofox-forecast 0.16.2 measured \
         0.9951 at the worst horizon) but worst_ratio={worst_ratio} -- the upstream fix from \
         PR #230/#229 may have regressed in a later anofox-forecast release"
    );
}
