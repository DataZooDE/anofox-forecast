//! Library-vs-FFI parity integration tests.
//!
//! For every model, we construct the `anofox-forecast` library model directly,
//! fit + predict on synthetic data, then call the FFI `anofox_ts_forecast()` with
//! identical inputs. The library is the source of truth — any mismatch in point
//! forecasts indicates a bug in the core/FFI translation layer.
//!
//! Prediction intervals, fitted values, and residuals now pass through from the
//! library's own `predict_with_intervals`/`fitted_values`/`residuals` (D-01/D-03/
//! D-11, Phase 10) — `anofox-fcst-core::forecast()` no longer recomputes them, so
//! `call_ffi_full` compares all five arrays bit-identically against the library.

use std::ffi::{c_char, c_double, CStr};

#[path = "../../anofox-fcst-core/tests/common/mod.rs"]
mod common;

use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
use anofox_forecast::models::baseline::{
    Naive, RandomWalkWithDrift, SeasonalNaive, SeasonalWindowAverage, SimpleMovingAverage,
};
use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, ETSSpec, HoltLinearTrend, HoltWinters, SeasonalES,
    SimpleExponentialSmoothing, ETS,
};
use anofox_forecast::models::garch::GARCH;
use anofox_forecast::models::intermittent::{Croston, ADIDA, IMAPA, TSB};
use anofox_forecast::models::kalman_forecaster::KalmanForecaster;
use anofox_forecast::models::laplace::LaplaceForecaster;
use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
use anofox_forecast::models::tbats::{AutoTBATS, TBATS};
use anofox_forecast::models::theta::{AutoTheta, DynamicTheta, OptimizedTheta, Theta};
use anofox_forecast::models::MFLES;
use anofox_forecast::prelude::Forecaster;
use chrono::{Duration, TimeZone, Utc};

use anofox_fcst_ffi::types::{
    AnofoxError, ExogenousData as FfiExogenousData, ExogenousRegressor as FfiExogenousRegressor,
    ForecastOptions as FfiForecastOptions, ForecastOptionsExog as FfiForecastOptionsExog,
    ForecastResult,
};

// Defined in anofox_fcst_ffi/src/lib.rs
extern "C" {
    fn anofox_ts_forecast(
        values: *const c_double,
        validity: *const u64,
        length: usize,
        options: *const FfiForecastOptions,
        out_result: *mut ForecastResult,
        out_error: *mut AnofoxError,
    ) -> bool;

    // D-10/FCST-05 (plan 10-04 Task 1): exogenous-regressor forecast entry
    // point, used by `exog_autoarima_parity_through_ffi` below.
    fn anofox_ts_forecast_exog(
        values: *const c_double,
        validity: *const u64,
        length: usize,
        options: *const FfiForecastOptionsExog,
        out_result: *mut ForecastResult,
        out_error: *mut AnofoxError,
    ) -> bool;

    fn anofox_free_forecast_result(result: *mut ForecastResult);
}

// ── Constants ──────────────────────────────────────────────────────────

const HORIZON: usize = 5;
const SEASONAL_PERIOD: usize = 12;

// ── Synthetic data generators ──────────────────────────────────────────

/// Seasonal time series: 60 points with period 12, trend, and noise.
fn seasonal_data() -> Vec<f64> {
    (0..60)
        .map(|i| {
            let trend = 10.0 + 0.15 * i as f64;
            let season = 5.0 * (2.0 * std::f64::consts::PI * i as f64 / 12.0).sin();
            let noise = ((i * 7 + 3) % 11) as f64 * 0.1 - 0.5; // deterministic "noise"
            trend + season + noise
        })
        .collect()
}

/// Intermittent demand data: mostly zeros with sporadic non-zero values.
fn intermittent_data() -> Vec<f64> {
    let pattern = [0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 5.0, 0.0, 2.0, 0.0, 0.0, 7.0];
    pattern.to_vec()
}

// ── Helpers ────────────────────────────────────────────────────────────

/// Build a `TimeSeries` from raw f64 values (hourly timestamps, matching forecast.rs).
fn make_timeseries(values: &[f64]) -> TimeSeries {
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec()).expect("valid timeseries")
}

/// Build an FFI `ForecastOptions` for a given model.
fn make_ffi_options(model_name: &str, horizon: i32, seasonal_period: i32) -> FfiForecastOptions {
    let mut opts = FfiForecastOptions::default();
    let bytes = model_name.as_bytes();
    for (i, &b) in bytes.iter().enumerate().take(31) {
        opts.model[i] = b as c_char;
    }
    opts.model[bytes.len().min(31)] = 0;
    opts.horizon = horizon;
    opts.seasonal_period = seasonal_period;
    opts.confidence_level = 0.95;
    opts.auto_detect_seasonality = false;
    // Point-forecast-only helper: most parity_* tests below only compare point
    // forecasts, so fitted/residuals are left un-requested here by default.
    opts.include_fitted = false;
    opts.include_residuals = false;
    opts
}

/// Build FFI options with explicit SMA window.
fn make_ffi_options_with_window(
    model: &str,
    horizon: i32,
    seasonal_period: i32,
    window: i32,
) -> FfiForecastOptions {
    let mut opts = make_ffi_options(model, horizon, seasonal_period);
    opts.window = window;
    opts
}

/// Build FFI options with an explicit ETS spec string (e.g. "AAA", "MNM").
fn make_ffi_options_with_ets(spec: &str, horizon: i32, seasonal_period: i32) -> FfiForecastOptions {
    let mut opts = make_ffi_options("ETS", horizon, seasonal_period);
    let bytes = spec.as_bytes();
    for (i, &b) in bytes.iter().enumerate().take(7) {
        opts.ets_model[i] = b as c_char;
    }
    opts.ets_model[bytes.len().min(7)] = 0;
    opts
}

/// Build FFI options with a seasonal_periods_str (e.g. "[6, 12]").
fn make_ffi_options_with_periods(
    model: &str,
    horizon: i32,
    periods_str: &str,
) -> FfiForecastOptions {
    let mut opts = make_ffi_options(model, horizon, 0);
    let bytes = periods_str.as_bytes();
    for (i, &b) in bytes.iter().enumerate().take(63) {
        opts.seasonal_periods_str[i] = b as c_char;
    }
    opts.seasonal_periods_str[bytes.len().min(63)] = 0;
    opts
}

/// Write a `&str` into a fixed-size `[c_char; N]` buffer, NUL-terminated.
fn write_c_field<const N: usize>(buf: &mut [c_char; N], s: &str) {
    let bytes = s.as_bytes();
    let max = N - 1;
    for (i, &b) in bytes.iter().enumerate().take(max) {
        buf[i] = b as c_char;
    }
    buf[bytes.len().min(max)] = 0;
}

/// Build FFI options for ARIMA with `arima_order`/`seasonal_order` strings
/// (e.g. "[0,1,1]") plus `seasonal_period` (D-08/D-09).
fn make_ffi_options_with_orders(
    model: &str,
    horizon: i32,
    seasonal_period: i32,
    arima_order: &str,
    seasonal_order: &str,
) -> FfiForecastOptions {
    let mut opts = make_ffi_options(model, horizon, seasonal_period);
    write_c_field(&mut opts.arima_order_str, arima_order);
    write_c_field(&mut opts.seasonal_order_str, seasonal_order);
    opts
}

/// Call the FFI function and return point forecasts + model name.
fn call_ffi(data: &[f64], opts: &FfiForecastOptions) -> (Vec<f64>, String) {
    let n_words = data.len().div_ceil(64);
    let validity: Vec<u64> = vec![u64::MAX; n_words];

    let mut result = ForecastResult::default();
    let mut error = AnofoxError::default();

    let ok = unsafe {
        anofox_ts_forecast(
            data.as_ptr(),
            validity.as_ptr(),
            data.len(),
            opts as *const _,
            &mut result as *mut _,
            &mut error as *mut _,
        )
    };

    if !ok {
        let msg = unsafe { CStr::from_ptr(error.message.as_ptr()) }
            .to_str()
            .unwrap_or("unknown");
        panic!("FFI call failed: {msg}");
    }

    let n = result.n_forecasts;
    let point = unsafe { std::slice::from_raw_parts(result.point_forecasts, n).to_vec() };
    let model_name = unsafe { CStr::from_ptr(result.model_name.as_ptr()) }
        .to_str()
        .unwrap_or("")
        .to_string();

    unsafe {
        anofox_free_forecast_result(&mut result as *mut _);
    }

    (point, model_name)
}

/// point, lower, upper, fitted, residuals, model_name
type FullForecastTuple = (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>, String);

/// Call the FFI function and return point/lower/upper/fitted/residuals + model name.
/// Null result pointers (D-02: a model with no native interval/fitted/residual
/// support) become empty `Vec`s, matching the core's own empty-vec convention.
/// Callers requesting fitted/residuals must set `include_fitted`/`include_residuals`
/// on `opts` themselves.
fn call_ffi_full(data: &[f64], opts: &FfiForecastOptions) -> FullForecastTuple {
    let n_words = data.len().div_ceil(64);
    let validity: Vec<u64> = vec![u64::MAX; n_words];

    let mut result = ForecastResult::default();
    let mut error = AnofoxError::default();

    let ok = unsafe {
        anofox_ts_forecast(
            data.as_ptr(),
            validity.as_ptr(),
            data.len(),
            opts as *const _,
            &mut result as *mut _,
            &mut error as *mut _,
        )
    };

    if !ok {
        let msg = unsafe { CStr::from_ptr(error.message.as_ptr()) }
            .to_str()
            .unwrap_or("unknown");
        panic!("FFI call failed: {msg}");
    }

    let n = result.n_forecasts;
    let point = unsafe { std::slice::from_raw_parts(result.point_forecasts, n).to_vec() };
    let lower = if result.lower_bounds.is_null() {
        vec![]
    } else {
        unsafe { std::slice::from_raw_parts(result.lower_bounds, n).to_vec() }
    };
    let upper = if result.upper_bounds.is_null() {
        vec![]
    } else {
        unsafe { std::slice::from_raw_parts(result.upper_bounds, n).to_vec() }
    };
    let fitted = if result.fitted_values.is_null() {
        vec![]
    } else {
        unsafe { std::slice::from_raw_parts(result.fitted_values, result.n_fitted).to_vec() }
    };
    let residuals = if result.residuals.is_null() {
        vec![]
    } else {
        // n_residuals is its OWN length field (plan 10-02 fix) — never
        // n_fitted, which can legitimately differ (e.g. GARCH: fitted=None
        // so n_fitted=0, but residuals is real and non-empty).
        unsafe { std::slice::from_raw_parts(result.residuals, result.n_residuals).to_vec() }
    };
    let model_name = unsafe { CStr::from_ptr(result.model_name.as_ptr()) }
        .to_str()
        .unwrap_or("")
        .to_string();

    unsafe {
        anofox_free_forecast_result(&mut result as *mut _);
    }

    (point, lower, upper, fitted, residuals, model_name)
}

/// Fit a library model and return point forecasts.
fn lib_predict(model: &mut dyn Forecaster, ts: &TimeSeries, horizon: usize) -> Vec<f64> {
    model
        .fit(ts)
        .unwrap_or_else(|e| panic!("[{}] library fit failed: {e}", model.name()));
    let forecast = model
        .predict(horizon)
        .unwrap_or_else(|e| panic!("[{}] library predict failed: {e}", model.name()));
    forecast.primary().to_vec()
}

/// point, lower, upper, fitted, residuals
type LibFullTuple = (Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>, Vec<f64>);

/// Fit a library model and return point/lower/upper/fitted/residuals via
/// `predict_with_intervals(horizon, level)` + `fitted_values()`/`residuals()` —
/// the same crate-trait path `extract_forecast` uses in forecast.rs (D-01/D-03/
/// D-11, Phase 10). A model with no native interval/fitted/residual support
/// yields empty vecs, matching `extract_bounds`'s / `extract_forecast`'s
/// own empty-vec convention (D-02).
fn lib_predict_full(
    model: &mut dyn Forecaster,
    ts: &TimeSeries,
    horizon: usize,
    level: f64,
) -> LibFullTuple {
    model
        .fit(ts)
        .unwrap_or_else(|e| panic!("[{}] library fit failed: {e}", model.name()));
    let forecast = model
        .predict_with_intervals(horizon, level)
        .unwrap_or_else(|e| {
            panic!(
                "[{}] library predict_with_intervals failed: {e}",
                model.name()
            )
        });
    let point = forecast.primary().to_vec();
    let lower = forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let upper = forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lower = if lower.len() == point.len() {
        lower
    } else {
        vec![]
    };
    let upper = if upper.len() == point.len() {
        upper
    } else {
        vec![]
    };
    let fitted = model
        .fitted_values()
        .map(|v| v.to_vec())
        .unwrap_or_default();
    let residuals = model.residuals().map(|v| v.to_vec()).unwrap_or_default();
    (point, lower, upper, fitted, residuals)
}

/// Fit `model` directly and compare point/lower/upper/fitted/residuals against
/// the FFI's output for the same model/options bit-for-bit (D-01/D-02/D-03/
/// D-11). `ffi_opts` must already have `model`/`horizon`/`seasonal_period`/etc.
/// set; this helper forces `confidence_level = level` and
/// `include_fitted = include_residuals = true`.
fn assert_full_parity(
    label: &str,
    model: &mut dyn Forecaster,
    ts: &TimeSeries,
    horizon: usize,
    level: f64,
    data: &[f64],
    mut ffi_opts: FfiForecastOptions,
) {
    let (lib_point, lib_lower, lib_upper, lib_fitted, lib_residuals) =
        lib_predict_full(model, ts, horizon, level);

    ffi_opts.confidence_level = level;
    ffi_opts.include_fitted = true;
    ffi_opts.include_residuals = true;
    let (ffi_point, ffi_lower, ffi_upper, ffi_fitted, ffi_residuals, _) =
        call_ffi_full(data, &ffi_opts);

    assert_f64_eq(&format!("{label}.point"), &lib_point, &ffi_point);
    assert_f64_eq(&format!("{label}.lower"), &lib_lower, &ffi_lower);
    assert_f64_eq(&format!("{label}.upper"), &lib_upper, &ffi_upper);

    // `finalize_output` (T-10-02, plan 10-01) only EXPORTS fitted/residuals
    // when their length exactly matches `prep.values.len()` (== data.len()
    // here — this helper's callers always pass a clean, non-NULL series).
    // A crate model whose own fitted/residuals are shorter than the input
    // (ARIMA with d>=1 drops `d` differencing-warmup rows; AutoARIMA
    // similarly on some series) is INTENTIONALLY nulled at that boundary —
    // not a bug, and unrelated to the n_fitted/n_residuals field-aliasing
    // bug fixed in plan 10-02 (see `ForecastResult::n_residuals`, which this
    // helper also exercises: GARCH's fitted is None while its length-
    // matching residuals ARE now expected to survive, below).
    let expect_exported = |v: &[f64]| v.len() == data.len();

    if expect_exported(&lib_fitted) {
        assert_f64_eq(&format!("{label}.fitted"), &lib_fitted, &ffi_fitted);
    } else {
        assert!(
            ffi_fitted.is_empty(),
            "[{label}.fitted] lib length {} != data length {} (T-10-02 length \
             mismatch) should export as empty via FFI, got {} elements",
            lib_fitted.len(),
            data.len(),
            ffi_fitted.len()
        );
    }

    if expect_exported(&lib_residuals) {
        assert_f64_eq(
            &format!("{label}.residuals"),
            &lib_residuals,
            &ffi_residuals,
        );
    } else {
        assert!(
            ffi_residuals.is_empty(),
            "[{label}.residuals] lib length {} != data length {} (T-10-02 length \
             mismatch) should export as empty via FFI, got {} elements",
            lib_residuals.len(),
            data.len(),
            ffi_residuals.len()
        );
    }
}

/// Assert two f64 slices are bit-identical (or both NaN).
fn assert_f64_eq(label: &str, lib: &[f64], ffi: &[f64]) {
    assert_eq!(
        lib.len(),
        ffi.len(),
        "[{label}] length mismatch: lib={} ffi={}",
        lib.len(),
        ffi.len()
    );
    for (i, (l, f)) in lib.iter().zip(ffi.iter()).enumerate() {
        if l.is_nan() && f.is_nan() {
            continue;
        }
        assert_eq!(
            l.to_bits(),
            f.to_bits(),
            "[{label}] point[{i}] mismatch: lib={l} ffi={f}"
        );
    }
}

// ── Library-backed models: point forecasts must be bit-identical ────────
//
// These models are constructed in forecast.rs using the exact same library
// constructors. The FFI chain is: FFI → core → library → back.

#[test]
fn parity_ses() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SimpleExponentialSmoothing::new(0.3)
    let mut model = SimpleExponentialSmoothing::new(0.3);
    let ffi_opts = make_ffi_options("SES", HORIZON as i32, 0);
    assert_full_parity("SES", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_ses_optimized() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SimpleExponentialSmoothing::auto()
    let mut model = SimpleExponentialSmoothing::auto();
    let ffi_opts = make_ffi_options("SESOptimized", HORIZON as i32, 0);
    assert_full_parity(
        "SESOptimized",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_holt() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: HoltLinearTrend::auto()
    let mut model = HoltLinearTrend::auto();
    let ffi_opts = make_ffi_options("Holt", HORIZON as i32, 0);
    assert_full_parity("Holt", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_holt_winters() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: HoltWinters::auto(period.max(2), SeasonalType::Additive)
    let mut model = HoltWinters::auto(
        SEASONAL_PERIOD,
        anofox_forecast::models::exponential::SeasonalType::Additive,
    );
    let ffi_opts = make_ffi_options("HoltWinters", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "HoltWinters",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_seasonal_es() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SeasonalES::new(period.max(2))
    let mut model = SeasonalES::new(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("SeasonalES", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "SeasonalES",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_seasonal_es_optimized() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SeasonalES::optimized(period.max(2))
    let mut model = SeasonalES::optimized(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options(
        "SeasonalESOptimized",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    assert_full_parity(
        "SeasonalESOptimized",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Theta::seasonal(period) when period > 1
    let mut model = Theta::seasonal(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("Theta", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("Theta", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_optimized_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: OptimizedTheta::seasonal(period) when period > 1
    let mut model = OptimizedTheta::seasonal(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("OptimizedTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "OptimizedTheta",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_dynamic_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: DynamicTheta::seasonal(period) when period > 1
    let mut model = DynamicTheta::seasonal(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("DynamicTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "DynamicTheta",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_dynamic_optimized_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: DynamicTheta::seasonal_optimized(period) when period > 1
    let mut model = DynamicTheta::seasonal_optimized(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options(
        "DynamicOptimizedTheta",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    assert_full_parity(
        "DynamicOptimizedTheta",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_auto_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoTheta::seasonal(period) when period > 1
    let mut model = AutoTheta::seasonal(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("AutoTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoTheta", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_ets() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoETS::with_config(AutoETSConfig::with_period(period))
    let config = AutoETSConfig::with_period(SEASONAL_PERIOD);
    let mut model = AutoETS::with_config(config);
    let ffi_opts = make_ffi_options("AutoETS", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoETS", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_arima() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoARIMA::with_config(AutoARIMAConfig::default().with_seasonal_period(period))
    let config = AutoARIMAConfig::default().with_seasonal_period(SEASONAL_PERIOD);
    let mut model = AutoARIMA::with_config(config);
    let ffi_opts = make_ffi_options("AutoARIMA", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoARIMA", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_mfles() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: MFLES::new(periods) — with period > 1, uses vec![period]
    let mut model = MFLES::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("MFLES", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("MFLES", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_mfles() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoMFLES uses same MFLES::new(periods)
    let mut model = MFLES::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("AutoMFLES", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoMFLES", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_mstl() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: MSTLForecaster::new(periods) — with period > 1, uses vec![period]
    let mut model = MSTLForecaster::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("MSTL", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("MSTL", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_mstl() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoMSTL uses same MSTLForecaster::new(periods), different model_name
    let mut model = MSTLForecaster::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("AutoMSTL", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoMSTL", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_tbats() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: TBATS::new(periods) — with period > 1, uses vec![period]
    let mut model = TBATS::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("TBATS", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("TBATS", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_tbats() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoTBATS::new(periods) — with period > 1, uses vec![period]
    let mut model = AutoTBATS::new(vec![SEASONAL_PERIOD]);
    let ffi_opts = make_ffi_options("AutoTBATS", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("AutoTBATS", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

// ── Intermittent demand models ─────────────────────────────────────────

#[test]
fn parity_croston_classic() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new()
    let mut model = Croston::new();
    let ffi_opts = make_ffi_options("CrostonClassic", HORIZON as i32, 0);
    assert_full_parity(
        "CrostonClassic",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_croston_optimized() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new().optimized()
    let mut model = Croston::new().optimized();
    let ffi_opts = make_ffi_options("CrostonOptimized", HORIZON as i32, 0);
    assert_full_parity(
        "CrostonOptimized",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_croston_sba() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new().sba()
    let mut model = Croston::new().sba();
    let ffi_opts = make_ffi_options("CrostonSBA", HORIZON as i32, 0);
    assert_full_parity(
        "CrostonSBA",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_tsb() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: TSB::new()
    let mut model = TSB::new();
    let ffi_opts = make_ffi_options("TSB", HORIZON as i32, 0);
    assert_full_parity("TSB", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_adida() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: ADIDA::new()
    let mut model = ADIDA::new();
    let ffi_opts = make_ffi_options("ADIDA", HORIZON as i32, 0);
    assert_full_parity("ADIDA", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_imapa() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: IMAPA::new()
    let mut model = IMAPA::new();
    let ffi_opts = make_ffi_options("IMAPA", HORIZON as i32, 0);
    assert_full_parity("IMAPA", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

// ── Hand-rolled models: verify against library equivalents ─────────────
//
// These models have hand-rolled implementations in forecast.rs that do NOT
// call the library. Comparing against the library catches any drift.

#[test]
fn parity_naive() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let mut model = Naive::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("Naive", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("Naive", &lib_point, &ffi_point);

    // Full parity (D-01/D-03/D-11): lower/upper/fitted/residuals through the FFI
    // must be bit-identical to the crate Naive's own predict_with_intervals/
    // fitted_values/residuals — Naive is no longer hand-rolled in forecast.rs.
    let mut full_model = Naive::new();
    full_model.fit(&ts).expect("[Naive] library fit failed");
    let lib_forecast = full_model
        .predict_with_intervals(HORIZON, 0.95)
        .expect("[Naive] library predict_with_intervals failed");
    let lib_lower = lib_forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lib_upper = lib_forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lib_fitted = full_model
        .fitted_values()
        .map(|v| v.to_vec())
        .unwrap_or_default();
    let lib_residuals = full_model
        .residuals()
        .map(|v| v.to_vec())
        .unwrap_or_default();

    let mut full_opts = make_ffi_options("Naive", HORIZON as i32, 0);
    full_opts.include_fitted = true;
    full_opts.include_residuals = true;
    let (_, ffi_lower, ffi_upper, ffi_fitted, ffi_residuals, _) = call_ffi_full(&data, &full_opts);

    assert_f64_eq("Naive.lower", &lib_lower, &ffi_lower);
    assert_f64_eq("Naive.upper", &lib_upper, &ffi_upper);
    assert_f64_eq("Naive.fitted", &lib_fitted, &ffi_fitted);
    assert_f64_eq("Naive.residuals", &lib_residuals, &ffi_residuals);
}

/// FCST-01/FCST-03: Naive's prediction interval through the full FFI chain
/// (`anofox_ts_forecast`) must be within a known, documented tolerance of R's
/// `naive()` at both 80% and 95% confidence — not the old fabricated
/// `point ± z(step-table)·sd·√h` band (95% h=1 half-width ~234.32 on this
/// series; R/crate half-width is ~66.07, a >99% reduction).
///
/// Tolerance note (deviation from the plan's originally-stated 1e-6 bound):
/// `anofox-forecast` 0.15.10's `Naive::predict_with_intervals` computes sigma
/// identically to R (`sqrt(mean(diff^2))`, confirmed empirically — the two
/// sides agree to 1e-9 once the same z is used) but its internal `z` comes
/// from a private Abramowitz & Stegun 26.2.23 approximation
/// (`models/baseline/naive.rs::quantile_normal`), not an exact / statrs-backed
/// inverse normal CDF as `10-RESEARCH.md` §2 assumed. That approximation's
/// documented max error is ~4.5e-4 in z, which on this series' sigma (~33.71)
/// and horizon (up to sqrt(12)) bounds the lower/upper deviation from R at
/// ≤0.0504 absolute (verified by direct computation this session). Per D-01
/// ("pass crate intervals through unchanged, including models whose crate
/// intervals are known to be miscalibrated until Phase 11/17") the fix here is
/// passthrough, not patching the upstream crate's z — so the test tolerance is
/// set to 0.1 (comfortably above the known ~0.05 worst case, far below the
/// ~168 deviation the old fabricated band would produce) to prove the real fix
/// without chasing upstream-exact-z precision that is Phase 11/17's scope.
#[test]
fn naive_intervals_match_r_naive_through_ffi() {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    let values = common::f64_vec(&fx["inputs"]["values"]);
    let naive = &fx["models"]["naive"];
    let r_point = common::f64_vec(&naive["point"]);
    let r_lower_80 = common::f64_vec(&naive["lower_80"]);
    let r_upper_80 = common::f64_vec(&naive["upper_80"]);
    let r_lower_95 = common::f64_vec(&naive["lower_95"]);
    let r_upper_95 = common::f64_vec(&naive["upper_95"]);

    let tol = 0.1;

    for (level, r_lower, r_upper) in [
        (0.80_f64, &r_lower_80, &r_upper_80),
        (0.95_f64, &r_lower_95, &r_upper_95),
    ] {
        let mut opts = make_ffi_options("Naive", 12, 0);
        opts.confidence_level = level;
        let (ffi_point, ffi_lower, ffi_upper, _, _, model_name) = call_ffi_full(&values, &opts);

        assert_eq!(model_name, "Naive");
        assert_eq!(ffi_point.len(), 12, "expected 12-step horizon");
        assert_eq!(ffi_lower.len(), 12, "Naive must return native lower bounds");
        assert_eq!(ffi_upper.len(), 12, "Naive must return native upper bounds");

        for i in 0..12 {
            common::assert_close(
                ffi_point[i],
                r_point[i],
                tol,
                0.0,
                &format!("Naive.point[{i}] level={level}"),
            );
            common::assert_close(
                ffi_lower[i],
                r_lower[i],
                tol,
                0.0,
                &format!("Naive.lower[{i}] level={level}"),
            );
            common::assert_close(
                ffi_upper[i],
                r_upper[i],
                tol,
                0.0,
                &format!("Naive.upper[{i}] level={level}"),
            );
        }
    }
}

#[test]
fn parity_seasonal_naive() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let mut model = SeasonalNaive::new(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("SeasonalNaive", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "SeasonalNaive",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_sma() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: window = period.max(3) when window=0, so window=12
    let mut model = SimpleMovingAverage::new(SEASONAL_PERIOD);
    let ffi_opts = make_ffi_options("SMA", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("SMA", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_random_walk_drift() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let mut model = RandomWalkWithDrift::new();
    let ffi_opts = make_ffi_options("RandomWalkDrift", HORIZON as i32, 0);
    assert_full_parity(
        "RandomWalkDrift",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_seasonal_window_average() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // Hand-rolled uses all data: 60 points / 12 period = 5 complete seasons
    let n_seasons = seasonal_data().len() / SEASONAL_PERIOD;
    let mut model = SeasonalWindowAverage::new(SEASONAL_PERIOD, n_seasons);
    let ffi_opts = make_ffi_options(
        "SeasonalWindowAverage",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    assert_full_parity(
        "SeasonalWindowAverage",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

// ── ETS without spec: runs the crate AutoETS selection (D-05/FCST-07) ──
//
// 'ETS' without an explicit `ets_model` now means exactly the same thing as
// standalone 'AutoETS': the core's `new_auto_ets` shared constructor builds
// an identically-configured `AutoETS::with_config(AutoETSConfig::with_period(p))`
// for both call sites, so point/lower/upper/fitted/residuals are bit-identical
// to fitting that same crate model directly — never a hand-picked
// HoltWinters/Holt/SES substitute.

#[test]
fn parity_ets_default() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: new_auto_ets(period, None) == AutoETS::with_config(AutoETSConfig::with_period(period))
    let config = AutoETSConfig::with_period(SEASONAL_PERIOD);
    let mut model = AutoETS::with_config(config);
    let ffi_opts = make_ffi_options("ETS", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity("ETS", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);

    // model_name must be exactly the crate's own short_name() text (e.g.
    // "ETS(M,A,M)") — never "ETS(ETS(...))" (the old double-wrap bug) and
    // never carrying an "Auto" prefix (that's 'AutoETS's own model_name).
    let ffi_opts2 = make_ffi_options("ETS", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (_, model_name) = call_ffi(&data, &ffi_opts2);
    let matches_ets_pattern = model_name.starts_with("ETS(") && model_name.ends_with(')') && {
        let inner = &model_name[4..model_name.len() - 1];
        let parts: Vec<&str> = inner.split(',').collect();
        parts.len() == 3
            && matches!(parts[0], "A" | "M")
            && matches!(parts[1], "N" | "A" | "Ad")
            && matches!(parts[2], "N" | "A" | "M")
    };
    assert!(
        matches_ets_pattern,
        "model_name '{model_name}' does not match ^ETS\\([AM],(N|A|Ad),(N|A|M)\\)$"
    );
}

// ── ARIMA (real crate model, D-08/D-09): full parity + R airline parity ──

#[test]
fn parity_arima() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: ARIMA::new(1,1,1) via extract_forecast, non-seasonal
    // (arima_order_str="[1,1,1]", seasonal_period=0).
    let mut model = anofox_forecast::models::arima::ARIMA::new(1, 1, 1);
    let ffi_opts = make_ffi_options_with_orders("ARIMA", HORIZON as i32, 0, "[1,1,1]", "");
    assert_full_parity("ARIMA", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn arima_airline_matches_r_through_ffi() {
    // FCST-04: ARIMA(0,1,1)(0,1,1)[12] on AirPassengers, through
    // anofox_ts_forecast end to end, must match R's
    // `Arima(order=c(0,1,1), seasonal=c(0,1,1))` within the fixture's
    // model_point_rel tolerance (not widened if it misses — see module doc).
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    let air: Vec<f64> = common::f64_vec(&fx["inputs"]["values"]);
    let r_points: Vec<f64> = common::f64_vec(&fx["models"]["arima_airline"]["point"]);
    let rel_tol = common::tolerance(&fx, "model_point_rel");

    let ffi_opts =
        make_ffi_options_with_orders("ARIMA", r_points.len() as i32, 12, "[0,1,1]", "[0,1,1]");
    let (ffi_point, model_name) = call_ffi(&air, &ffi_opts);

    assert_eq!(
        model_name, "ARIMA(0,1,1)(0,1,1)[12]",
        "unexpected model_name"
    );
    common::assert_vec_close(&ffi_point, &r_points, 0.0, rel_tol, "arima_airline.point");

    // Bounds must bracket the point forecast at every step.
    let mut full_opts = ffi_opts;
    full_opts.include_fitted = true;
    full_opts.include_residuals = true;
    let (point, lower, upper, _fitted, _residuals, _) = call_ffi_full(&air, &full_opts);
    assert_eq!(lower.len(), point.len());
    assert_eq!(upper.len(), point.len());
    for i in 0..point.len() {
        assert!(
            lower[i] < point[i] && point[i] < upper[i],
            "step {i}: expected lower < point < upper, got {} < {} < {}",
            lower[i],
            point[i],
            upper[i]
        );
    }
}

// ── Classical / ensemble / distributional: full parity (D-01/D-02/D-03/D-11) ──

#[test]
fn parity_garch() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let level = 0.95;

    // forecast.rs: point = sqrt(forecast_variance(h)), NOT predict()/predict_with_intervals()
    // primary() — predict_with_intervals is used only for lower/upper (D-01).
    let mut model = GARCH::new(1, 1);
    model.fit(&ts).expect("[GARCH] library fit failed");
    let variance = model
        .forecast_variance(HORIZON)
        .expect("[GARCH] library forecast_variance failed");
    let lib_point: Vec<f64> = variance.iter().map(|&v| v.sqrt()).collect();
    let forecast = model
        .predict_with_intervals(HORIZON, level)
        .expect("[GARCH] library predict_with_intervals failed");
    let lib_lower = forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lib_upper = forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lib_lower = if lib_lower.len() == lib_point.len() {
        lib_lower
    } else {
        vec![]
    };
    let lib_upper = if lib_upper.len() == lib_point.len() {
        lib_upper
    } else {
        vec![]
    };
    let lib_fitted = model
        .fitted_values()
        .map(|v| v.to_vec())
        .unwrap_or_default();
    let lib_residuals = model.residuals().map(|v| v.to_vec()).unwrap_or_default();

    let mut ffi_opts = make_ffi_options("GARCH", HORIZON as i32, 0);
    ffi_opts.garch_p = 1;
    ffi_opts.garch_q = 1;
    ffi_opts.confidence_level = level;
    ffi_opts.include_fitted = true;
    ffi_opts.include_residuals = true;
    let (ffi_point, ffi_lower, ffi_upper, ffi_fitted, ffi_residuals, model_name) =
        call_ffi_full(&data, &ffi_opts);

    assert_eq!(model_name, "GARCH(1,1)");
    assert_f64_eq("GARCH.point", &lib_point, &ffi_point);
    assert_f64_eq("GARCH.lower", &lib_lower, &ffi_lower);
    assert_f64_eq("GARCH.upper", &lib_upper, &ffi_upper);
    // GARCH::fitted_values() is always None by crate design ("fitted values
    // are just the mean for GARCH") — not required by this plan's <behavior>
    // (GARCH is scoped to point/lower/upper only). Both sides are empty.
    assert_f64_eq("GARCH.fitted", &lib_fitted, &ffi_fitted);
    // FIXED in plan 10-02 (see assert_full_parity's doc comment): fitted is
    // always None for GARCH, but residuals now has its own n_residuals
    // length field, so the crate's real residuals survive the FFI boundary
    // even though fitted doesn't.
    assert!(
        !lib_residuals.is_empty(),
        "GARCH.residuals: expected the crate to produce real residuals — got none; \
         if GARCH now exposes no residuals either, this assertion is stale"
    );
    assert_f64_eq("GARCH.residuals", &lib_residuals, &ffi_residuals);
}

#[test]
fn parity_kalman() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: KalmanForecaster::local_level() (default spec) via extract_forecast
    let mut model = KalmanForecaster::local_level();
    let ffi_opts = make_ffi_options("Kalman", HORIZON as i32, 0);
    assert_full_parity("Kalman", &mut model, &ts, HORIZON, 0.95, &data, ffi_opts);
}

#[test]
fn parity_auto_ensemble() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoEnsemble::with_config { top_k: 3, Mean, seasonal_period }
    // via extract_forecast (Phase 4 default top_k/method, matches forecast_with_model).
    let config = AutoEnsembleConfig {
        top_k: 3,
        combination_method: anofox_forecast::models::ensemble::CombinationMethod::Mean,
        seasonal_period: Some(SEASONAL_PERIOD),
    };
    let mut model = AutoEnsemble::with_config(config);
    let ffi_opts = make_ffi_options("AutoEnsemble", HORIZON as i32, SEASONAL_PERIOD as i32);
    assert_full_parity(
        "AutoEnsemble",
        &mut model,
        &ts,
        HORIZON,
        0.95,
        &data,
        ffi_opts,
    );
}

#[test]
fn parity_laplace() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let level = 0.95;

    // forecast.rs forecast_laplace: LaplaceForecaster::new().auto().with_seasonal(period),
    // clamped confidence_level in [0.5, 0.999] (no clamping needed at 0.95).
    let mut model = LaplaceForecaster::new()
        .auto()
        .with_seasonal(SEASONAL_PERIOD);
    let (lib_point, lib_lower, lib_upper, lib_fitted, lib_residuals) =
        lib_predict_full(&mut model, &ts, HORIZON, level);

    let mut ffi_opts = make_ffi_options("Laplace", HORIZON as i32, SEASONAL_PERIOD as i32);
    ffi_opts.confidence_level = level;
    ffi_opts.include_fitted = true;
    ffi_opts.include_residuals = true;
    let (ffi_point, ffi_lower, ffi_upper, ffi_fitted, ffi_residuals, model_name) =
        call_ffi_full(&data, &ffi_opts);

    assert_eq!(
        model_name,
        format!("Laplace(auto,seasonal={SEASONAL_PERIOD})")
    );
    assert_f64_eq("Laplace.point", &lib_point, &ffi_point);
    assert_f64_eq("Laplace.lower", &lib_lower, &ffi_lower);
    assert_f64_eq("Laplace.upper", &lib_upper, &ffi_upper);
    assert_f64_eq("Laplace.fitted", &lib_fitted, &ffi_fitted);
    assert_f64_eq("Laplace.residuals", &lib_residuals, &ffi_residuals);
}

// ── Exogenous regressors (D-10/FCST-05, plan 10-04) ─────────────────────

/// `anofox_ts_forecast_exog` with one regressor (`validity`/`future_validity`
/// both null — Task 1 scope, read starting Task 2) for `AutoARIMA` is
/// bit-identical to the crate `AutoARIMA` fitted directly on a `TimeSeries`
/// carrying the same regressor via `CalendarAnnotations` and predicted with
/// `predict_with_exog_intervals` — proving the FFI/C-core round trip adds no
/// discrepancy on top of the `forecast_exog.rs` `autoarima_exog_runs_order_search`
/// coverage of `forecast_with_exog` itself.
#[test]
fn exog_autoarima_parity_through_ffi() {
    use anofox_forecast::core::CalendarAnnotations;
    use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
    use std::collections::HashMap;

    let n = 120usize;
    let horizon = 6usize;
    let total = n + horizon;

    let x1: Vec<f64> = (0..total)
        .map(|i| (i as f64 * 0.37).sin() + i as f64 * 0.01)
        .collect();
    let mut y = Vec::with_capacity(n);
    let mut ar = 0.0_f64;
    let ar_coef = 0.5_f64;
    for (i, &x1_val) in x1.iter().take(n).enumerate() {
        let noise = (((i as u64 + 1).wrapping_mul(97) % 1000) as f64 - 500.0) / 500.0;
        ar = ar_coef * ar + noise;
        y.push(50.0 + 3.0 * x1_val + ar);
    }
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());

    // ── Through the FFI ──────────────────────────────────────────────
    let regressor = FfiExogenousRegressor {
        values: x1_hist.as_ptr(),
        n_values: x1_hist.len(),
        future_values: x1_future.as_ptr(),
        n_future: x1_future.len(),
        validity: std::ptr::null(),
        future_validity: std::ptr::null(),
    };
    let exog_data = FfiExogenousData {
        regressors: &regressor as *const FfiExogenousRegressor,
        n_regressors: 1,
    };
    let mut opts = FfiForecastOptionsExog::default();
    write_c_field(&mut opts.model, "AutoARIMA");
    opts.horizon = horizon as i32;
    opts.auto_detect_seasonality = false;
    opts.seasonal_period = 0;
    opts.confidence_level = 0.95;
    opts.exog = &exog_data as *const FfiExogenousData;

    let n_words = y.len().div_ceil(64);
    let validity: Vec<u64> = vec![u64::MAX; n_words];
    let mut result = ForecastResult::default();
    let mut error = AnofoxError::default();
    let ok = unsafe {
        anofox_ts_forecast_exog(
            y.as_ptr(),
            validity.as_ptr(),
            y.len(),
            &opts as *const _,
            &mut result as *mut _,
            &mut error as *mut _,
        )
    };
    assert!(ok, "anofox_ts_forecast_exog failed: {:?}", unsafe {
        CStr::from_ptr(error.message.as_ptr())
    });

    let n_forecasts = result.n_forecasts;
    let ffi_point =
        unsafe { std::slice::from_raw_parts(result.point_forecasts, n_forecasts).to_vec() };
    let ffi_lower = if result.lower_bounds.is_null() {
        vec![]
    } else {
        unsafe { std::slice::from_raw_parts(result.lower_bounds, n_forecasts).to_vec() }
    };
    let ffi_upper = if result.upper_bounds.is_null() {
        vec![]
    } else {
        unsafe { std::slice::from_raw_parts(result.upper_bounds, n_forecasts).to_vec() }
    };
    let model_name = unsafe { CStr::from_ptr(result.model_name.as_ptr()) }
        .to_str()
        .unwrap_or("")
        .to_string();
    unsafe {
        anofox_free_forecast_result(&mut result as *mut _);
    }

    assert!(
        model_name.ends_with(" with regressors"),
        "model_name '{model_name}' should end with ' with regressors'"
    );

    // ── Direct crate fit/predict with the SAME regressor attached ───────
    let ts_timestamps = {
        let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
        (0..n)
            .map(|i| base + Duration::hours(i as i64))
            .collect::<Vec<_>>()
    };
    let calendar = CalendarAnnotations::new().with_regressor("x000".to_string(), x1_hist.clone());
    let mut ts = TimeSeries::univariate(ts_timestamps, y.clone()).expect("valid TimeSeries");
    ts.set_calendar(calendar);

    let mut direct_model = AutoARIMA::with_config(AutoARIMAConfig::default());
    direct_model
        .fit(&ts)
        .expect("direct AutoARIMA fit with regressor should succeed");
    let mut future_map = HashMap::new();
    future_map.insert("x000".to_string(), x1_future.clone());
    let direct_forecast = direct_model
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct AutoARIMA predict_with_exog_intervals should succeed");

    let direct_point = direct_forecast.primary().to_vec();
    let direct_lower = direct_forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let direct_upper = direct_forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();

    assert_f64_eq("exog_autoarima_ffi.point", &ffi_point, &direct_point);
    if direct_lower.len() == direct_point.len() {
        assert_f64_eq("exog_autoarima_ffi.lower", &ffi_lower, &direct_lower);
    }
    if direct_upper.len() == direct_point.len() {
        assert_f64_eq("exog_autoarima_ffi.upper", &ffi_upper, &direct_upper);
    }
}

// ── Parameter grid tests ───────────────────────────────────────────────
//
// Each test sweeps a parameter across multiple values, verifying parity
// between the library and FFI for every combination.

mod param_grid {
    use super::*;

    #[test]
    fn grid_sma_window() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        for window in [3, 5, 12, 30, 60] {
            let mut model = SimpleMovingAverage::new(window);
            let lib_point = lib_predict(&mut model, &ts, HORIZON);

            let ffi_opts = make_ffi_options_with_window("SMA", HORIZON as i32, 0, window as i32);
            let (ffi_point, _) = call_ffi(&data, &ffi_opts);
            // SMA is now crate-backed (D-01/D-11) — bit-identical, not ≤1 ULP.
            assert_f64_eq(&format!("SMA(window={window})"), &lib_point, &ffi_point);
        }
    }

    #[test]
    fn grid_seasonal_period() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        for period in [4_usize, 6, 12] {
            let label = |name: &str| format!("{name}(period={period})");

            // HoltWinters
            {
                let mut model = HoltWinters::auto(
                    period,
                    anofox_forecast::models::exponential::SeasonalType::Additive,
                );
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("HoltWinters", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("HoltWinters"), &lib_point, &ffi_point);
            }

            // SeasonalES
            {
                let mut model = SeasonalES::new(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("SeasonalES", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("SeasonalES"), &lib_point, &ffi_point);
            }

            // SeasonalESOptimized
            {
                let mut model = SeasonalES::optimized(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts =
                    make_ffi_options("SeasonalESOptimized", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("SeasonalESOptimized"), &lib_point, &ffi_point);
            }

            // SeasonalNaive
            {
                let mut model = SeasonalNaive::new(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("SeasonalNaive", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("SeasonalNaive"), &lib_point, &ffi_point);
            }

            // SeasonalWindowAverage
            {
                let n_seasons = data.len() / period;
                let mut model = SeasonalWindowAverage::new(period, n_seasons);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts =
                    make_ffi_options("SeasonalWindowAverage", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("SeasonalWindowAverage"), &lib_point, &ffi_point);
            }

            // Theta
            {
                let mut model = Theta::seasonal(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("Theta", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("Theta"), &lib_point, &ffi_point);
            }

            // OptimizedTheta
            {
                let mut model = OptimizedTheta::seasonal(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("OptimizedTheta", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("OptimizedTheta"), &lib_point, &ffi_point);
            }

            // DynamicTheta
            {
                let mut model = DynamicTheta::seasonal(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("DynamicTheta", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("DynamicTheta"), &lib_point, &ffi_point);
            }

            // DynamicOptimizedTheta
            {
                let mut model = DynamicTheta::seasonal_optimized(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts =
                    make_ffi_options("DynamicOptimizedTheta", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("DynamicOptimizedTheta"), &lib_point, &ffi_point);
            }

            // AutoTheta
            {
                let mut model = AutoTheta::seasonal(period);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("AutoTheta", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("AutoTheta"), &lib_point, &ffi_point);
            }

            // AutoETS
            {
                let config = AutoETSConfig::with_period(period);
                let mut model = AutoETS::with_config(config);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("AutoETS", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("AutoETS"), &lib_point, &ffi_point);
            }

            // AutoARIMA
            {
                let config = AutoARIMAConfig::default().with_seasonal_period(period);
                let mut model = AutoARIMA::with_config(config);
                let lib_point = lib_predict(&mut model, &ts, HORIZON);
                let ffi_opts = make_ffi_options("AutoARIMA", HORIZON as i32, period as i32);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(&label("AutoARIMA"), &lib_point, &ffi_point);
            }
        }
    }

    #[test]
    fn grid_ets_spec() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        for spec_str in ["AAA", "ANA", "MNM", "MAM", "AAdA", "MAdM"] {
            let spec = ETSSpec::from_notation(spec_str).unwrap();
            let seasonal_period = if spec.has_seasonal() && SEASONAL_PERIOD > 1 {
                SEASONAL_PERIOD
            } else {
                1
            };

            let mut model = ETS::new(spec, seasonal_period);
            let lib_point = lib_predict(&mut model, &ts, HORIZON);

            let ffi_opts =
                make_ffi_options_with_ets(spec_str, HORIZON as i32, SEASONAL_PERIOD as i32);
            let (ffi_point, _) = call_ffi(&data, &ffi_opts);
            assert_f64_eq(&format!("ETS({spec_str})"), &lib_point, &ffi_point);
        }
    }

    #[test]
    fn grid_multi_seasonal_periods() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        let period_combos: &[(&str, Vec<usize>)] = &[
            ("[6]", vec![6]),
            ("[12]", vec![12]),
            ("[6, 12]", vec![6, 12]),
        ];

        let models: &[&str] = &[
            "MSTL",
            "AutoMSTL",
            "TBATS",
            "AutoTBATS",
            "MFLES",
            "AutoMFLES",
        ];

        for (periods_str, periods) in period_combos {
            for &model_name in models {
                let mut model: Box<dyn Forecaster> = match model_name {
                    "MSTL" | "AutoMSTL" => Box::new(MSTLForecaster::new(periods.clone())),
                    "TBATS" => Box::new(TBATS::new(periods.clone())),
                    "AutoTBATS" => Box::new(AutoTBATS::new(periods.clone())),
                    "MFLES" | "AutoMFLES" => Box::new(MFLES::new(periods.clone())),
                    _ => unreachable!(),
                };
                let lib_point = lib_predict(model.as_mut(), &ts, HORIZON);

                let ffi_opts =
                    make_ffi_options_with_periods(model_name, HORIZON as i32, periods_str);
                let (ffi_point, _) = call_ffi(&data, &ffi_opts);
                assert_f64_eq(
                    &format!("{model_name}(periods={periods_str})"),
                    &lib_point,
                    &ffi_point,
                );
            }
        }
    }

    #[test]
    fn grid_horizon() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        for horizon in [1_usize, 3, 10, 20] {
            let mut model = SimpleExponentialSmoothing::new(0.3);
            let lib_point = lib_predict(&mut model, &ts, horizon);

            let ffi_opts = make_ffi_options("SES", horizon as i32, 0);
            let (ffi_point, _) = call_ffi(&data, &ffi_opts);
            assert_eq!(
                lib_point.len(),
                horizon,
                "SES(horizon={horizon}) lib length mismatch"
            );
            assert_eq!(
                ffi_point.len(),
                horizon,
                "SES(horizon={horizon}) ffi length mismatch"
            );
            assert_f64_eq(&format!("SES(horizon={horizon})"), &lib_point, &ffi_point);
        }
    }

    #[test]
    fn grid_theta_nonseasonal() {
        let data = seasonal_data();
        let ts = make_timeseries(&data);

        let models: Vec<(&str, Box<dyn Forecaster>)> = vec![
            ("Theta", Box::new(Theta::new())),
            ("OptimizedTheta", Box::new(OptimizedTheta::new())),
            ("DynamicTheta", Box::new(DynamicTheta::new(0.1))),
            ("DynamicOptimizedTheta", Box::new(DynamicTheta::optimized())),
            ("AutoTheta", Box::new(AutoTheta::new())),
        ];

        for (model_name, mut model) in models {
            let lib_point = lib_predict(model.as_mut(), &ts, HORIZON);

            let ffi_opts = make_ffi_options(model_name, HORIZON as i32, 0);
            let (ffi_point, _) = call_ffi(&data, &ffi_opts);
            assert_f64_eq(
                &format!("{model_name}(nonseasonal)"),
                &lib_point,
                &ffi_point,
            );
        }
    }
}
