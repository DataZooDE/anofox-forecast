//! Library-vs-FFI parity integration tests.
//!
//! For every model, we construct the `anofox-forecast` library model directly,
//! fit + predict on synthetic data, then call the FFI `anofox_ts_forecast()` with
//! identical inputs. The library is the source of truth — any mismatch in point
//! forecasts indicates a bug in the core/FFI translation layer.
//!
//! Note: confidence intervals, fitted values, and residuals are recalculated by
//! `anofox-fcst-core::forecast()` independently of the library, so only point
//! forecasts are compared here.

use std::ffi::{c_char, c_double, CStr};

use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
use anofox_forecast::models::baseline::{
    Naive, RandomWalkWithDrift, SeasonalNaive, SeasonalWindowAverage, SimpleMovingAverage,
};
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, ETSSpec, HoltLinearTrend, HoltWinters, SeasonalES,
    SimpleExponentialSmoothing, ETS,
};
use anofox_forecast::models::intermittent::{Croston, ADIDA, IMAPA, TSB};
use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
use anofox_forecast::models::tbats::{AutoTBATS, TBATS};
use anofox_forecast::models::theta::{AutoTheta, DynamicTheta, OptimizedTheta, Theta};
use anofox_forecast::models::MFLES;
use anofox_forecast::prelude::Forecaster;
use chrono::{Duration, TimeZone, Utc};

use anofox_fcst_ffi::types::{AnofoxError, ForecastOptions as FfiForecastOptions, ForecastResult};

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
    // Don't request fitted/residuals — those are recalculated by core, not from library
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

/// Assert two f64 slices are approximately equal (within `max_ulps` ULPs).
/// Use for hand-rolled models where floating-point summation order may differ.
fn assert_f64_near(label: &str, lib: &[f64], ffi: &[f64], max_ulps: i64) {
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
        let diff = (l.to_bits() as i64).wrapping_sub(f.to_bits() as i64).abs();
        assert!(
            diff <= max_ulps,
            "[{label}] point[{i}] mismatch beyond {max_ulps} ULPs: lib={l} ffi={f} (diff={diff} ULPs)"
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
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("SES", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SES", &lib_point, &ffi_point);
}

#[test]
fn parity_ses_optimized() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SimpleExponentialSmoothing::auto()
    let mut model = SimpleExponentialSmoothing::auto();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("SESOptimized", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SESOptimized", &lib_point, &ffi_point);
}

#[test]
fn parity_holt() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: HoltLinearTrend::auto()
    let mut model = HoltLinearTrend::auto();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("Holt", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("Holt", &lib_point, &ffi_point);
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
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("HoltWinters", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("HoltWinters", &lib_point, &ffi_point);
}

#[test]
fn parity_seasonal_es() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SeasonalES::new(period.max(2))
    let mut model = SeasonalES::new(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("SeasonalES", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SeasonalES", &lib_point, &ffi_point);
}

#[test]
fn parity_seasonal_es_optimized() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: SeasonalES::optimized(period.max(2))
    let mut model = SeasonalES::optimized(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options(
        "SeasonalESOptimized",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SeasonalESOptimized", &lib_point, &ffi_point);
}

#[test]
fn parity_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Theta::seasonal(period) when period > 1
    let mut model = Theta::seasonal(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("Theta", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("Theta", &lib_point, &ffi_point);
}

#[test]
fn parity_optimized_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: OptimizedTheta::seasonal(period) when period > 1
    let mut model = OptimizedTheta::seasonal(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("OptimizedTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("OptimizedTheta", &lib_point, &ffi_point);
}

#[test]
fn parity_dynamic_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: DynamicTheta::seasonal(period) when period > 1
    let mut model = DynamicTheta::seasonal(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("DynamicTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("DynamicTheta", &lib_point, &ffi_point);
}

#[test]
fn parity_dynamic_optimized_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: DynamicTheta::seasonal_optimized(period) when period > 1
    let mut model = DynamicTheta::seasonal_optimized(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options(
        "DynamicOptimizedTheta",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("DynamicOptimizedTheta", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_theta() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoTheta::seasonal(period) when period > 1
    let mut model = AutoTheta::seasonal(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoTheta", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoTheta", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_ets() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoETS::with_config(AutoETSConfig::with_period(period))
    let config = AutoETSConfig::with_period(SEASONAL_PERIOD);
    let mut model = AutoETS::with_config(config);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoETS", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoETS", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_arima() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoARIMA::with_config(AutoARIMAConfig::default().with_seasonal_period(period))
    let config = AutoARIMAConfig::default().with_seasonal_period(SEASONAL_PERIOD);
    let mut model = AutoARIMA::with_config(config);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoARIMA", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoARIMA", &lib_point, &ffi_point);
}

#[test]
fn parity_mfles() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: MFLES::new(periods) — with period > 1, uses vec![period]
    let mut model = MFLES::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("MFLES", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("MFLES", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_mfles() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoMFLES uses same MFLES::new(periods)
    let mut model = MFLES::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoMFLES", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoMFLES", &lib_point, &ffi_point);
}

#[test]
fn parity_mstl() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: MSTLForecaster::new(periods) — with period > 1, uses vec![period]
    let mut model = MSTLForecaster::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("MSTL", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("MSTL", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_mstl() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoMSTL uses same MSTLForecaster::new(periods), different model_name
    let mut model = MSTLForecaster::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoMSTL", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoMSTL", &lib_point, &ffi_point);
}

#[test]
fn parity_tbats() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: TBATS::new(periods) — with period > 1, uses vec![period]
    let mut model = TBATS::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("TBATS", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("TBATS", &lib_point, &ffi_point);
}

#[test]
fn parity_auto_tbats() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: AutoTBATS::new(periods) — with period > 1, uses vec![period]
    let mut model = AutoTBATS::new(vec![SEASONAL_PERIOD]);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("AutoTBATS", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("AutoTBATS", &lib_point, &ffi_point);
}

// ── Intermittent demand models ─────────────────────────────────────────

#[test]
fn parity_croston_classic() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new()
    let mut model = Croston::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("CrostonClassic", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("CrostonClassic", &lib_point, &ffi_point);
}

#[test]
fn parity_croston_optimized() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new().optimized()
    let mut model = Croston::new().optimized();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("CrostonOptimized", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("CrostonOptimized", &lib_point, &ffi_point);
}

#[test]
fn parity_croston_sba() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: Croston::new().sba()
    let mut model = Croston::new().sba();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("CrostonSBA", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("CrostonSBA", &lib_point, &ffi_point);
}

#[test]
fn parity_tsb() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: TSB::new()
    let mut model = TSB::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("TSB", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("TSB", &lib_point, &ffi_point);
}

#[test]
fn parity_adida() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: ADIDA::new()
    let mut model = ADIDA::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("ADIDA", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("ADIDA", &lib_point, &ffi_point);
}

#[test]
fn parity_imapa() {
    let data = intermittent_data();
    let ts = make_timeseries(&data);
    // forecast.rs: IMAPA::new()
    let mut model = IMAPA::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("IMAPA", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("IMAPA", &lib_point, &ffi_point);
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
}

#[test]
fn parity_seasonal_naive() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let mut model = SeasonalNaive::new(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("SeasonalNaive", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SeasonalNaive", &lib_point, &ffi_point);
}

#[test]
fn parity_sma() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // forecast.rs: window = period.max(3) when window=0, so window=12
    let mut model = SimpleMovingAverage::new(SEASONAL_PERIOD);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("SMA", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SMA", &lib_point, &ffi_point);
}

#[test]
fn parity_random_walk_drift() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    let mut model = RandomWalkWithDrift::new();
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("RandomWalkDrift", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("RandomWalkDrift", &lib_point, &ffi_point);
}

#[test]
fn parity_seasonal_window_average() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // Hand-rolled uses all data: 60 points / 12 period = 5 complete seasons
    let n_seasons = seasonal_data().len() / SEASONAL_PERIOD;
    let mut model = SeasonalWindowAverage::new(SEASONAL_PERIOD, n_seasons);
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options(
        "SeasonalWindowAverage",
        HORIZON as i32,
        SEASONAL_PERIOD as i32,
    );
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("SeasonalWindowAverage", &lib_point, &ffi_point);
}

// ── ETS without spec: falls back to HoltWinters/Holt/SES in core ──────
//
// ETS (no ets_spec) in forecast.rs uses a fallback chain:
//   period > 1 && len >= 2*period → HoltWinters
//   len >= 10 → Holt
//   else → SES
// Our seasonal_data() has 60 points with period 12 → HoltWinters path.

#[test]
fn parity_ets_default() {
    let data = seasonal_data();
    let ts = make_timeseries(&data);
    // ETS without spec falls back to HoltWinters::auto(period, Additive) for our data
    let mut model = HoltWinters::auto(
        SEASONAL_PERIOD,
        anofox_forecast::models::exponential::SeasonalType::Additive,
    );
    let lib_point = lib_predict(&mut model, &ts, HORIZON);

    let ffi_opts = make_ffi_options("ETS", HORIZON as i32, SEASONAL_PERIOD as i32);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    assert_f64_eq("ETS", &lib_point, &ffi_point);
}

// ── ARIMA (hand-rolled simplified): may diverge from library ───────────
//
// forecast.rs uses a simplified ARIMA(1,1,1) with fixed AR coefficient.
// The library ARIMA is a proper implementation. Compare to detect drift.

#[test]
fn parity_arima() {
    let data = seasonal_data();

    // Hand-rolled ARIMA from forecast.rs: simplified ARIMA(1,1,1) with ar_coef=0.5
    let diff: Vec<f64> = data.windows(2).map(|w| w[1] - w[0]).collect();
    let mean_diff = diff.iter().sum::<f64>() / diff.len() as f64;
    let ar_coef = 0.5;
    let last_val = *data.last().unwrap();
    let last_diff = *diff.last().unwrap();

    let mut expected = Vec::with_capacity(HORIZON);
    let mut prev_diff = last_diff;
    let mut cumsum = last_val;
    for _ in 0..HORIZON {
        let next_diff = mean_diff + ar_coef * (prev_diff - mean_diff);
        cumsum += next_diff;
        expected.push(cumsum);
        prev_diff = next_diff;
    }

    let ffi_opts = make_ffi_options("ARIMA", HORIZON as i32, 0);
    let (ffi_point, _) = call_ffi(&data, &ffi_opts);
    // ARIMA is hand-rolled, so compare against the hand-rolled expected values
    assert_f64_eq("ARIMA", &expected, &ffi_point);
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
            // SMA is hand-rolled in core; summation order may differ by ≤1 ULP.
            assert_f64_near(&format!("SMA(window={window})"), &lib_point, &ffi_point, 1);
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
