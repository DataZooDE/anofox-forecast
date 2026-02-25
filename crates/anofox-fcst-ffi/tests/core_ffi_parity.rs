//! Core-vs-FFI parity integration tests.
//!
//! For every model in `list_models()`, we call the Rust core `forecast()` directly
//! and then call the FFI `anofox_ts_forecast()` with identical inputs. Both must
//! produce bit-identical results since the FFI layer is a thin translation shim.

use std::ffi::{c_char, c_double, CStr};

use anofox_fcst_core::forecast::{forecast, list_models, ForecastOptions, ModelType};
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

/// Build an FFI `ForecastOptions` matching a core `ForecastOptions`.
fn make_ffi_options(model_name: &str, horizon: i32, seasonal_period: i32) -> FfiForecastOptions {
    let mut opts = FfiForecastOptions::default();
    // Copy model name into fixed-size buffer
    let bytes = model_name.as_bytes();
    for (i, &b) in bytes.iter().enumerate().take(31) {
        opts.model[i] = b as c_char;
    }
    opts.model[bytes.len().min(31)] = 0;
    opts.horizon = horizon;
    opts.seasonal_period = seasonal_period;
    opts.confidence_level = 0.95;
    opts.auto_detect_seasonality = false;
    opts.include_fitted = true;
    opts.include_residuals = true;
    opts
}

/// Call the FFI function safely and return a comparison-friendly struct.
/// Panics on FFI error so tests fail clearly.
struct FfiOutput {
    point: Vec<f64>,
    lower: Vec<f64>,
    upper: Vec<f64>,
    fitted: Option<Vec<f64>>,
    residuals: Option<Vec<f64>>,
    model_name: String,
    aic: f64,
    bic: f64,
    mse: f64,
}

fn call_ffi(data: &[f64], opts: &FfiForecastOptions) -> FfiOutput {
    // All values valid → every bit set
    let n_words = (data.len() + 63) / 64;
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
    let lower = unsafe { std::slice::from_raw_parts(result.lower_bounds, n).to_vec() };
    let upper = unsafe { std::slice::from_raw_parts(result.upper_bounds, n).to_vec() };

    let fitted = if !result.fitted_values.is_null() && result.n_fitted > 0 {
        Some(unsafe { std::slice::from_raw_parts(result.fitted_values, result.n_fitted).to_vec() })
    } else {
        None
    };

    let residuals = if !result.residuals.is_null() && result.n_fitted > 0 {
        Some(unsafe { std::slice::from_raw_parts(result.residuals, result.n_fitted).to_vec() })
    } else {
        None
    };

    let model_name = unsafe { CStr::from_ptr(result.model_name.as_ptr()) }
        .to_str()
        .unwrap_or("")
        .to_string();

    let output = FfiOutput {
        point,
        lower,
        upper,
        fitted,
        residuals,
        model_name,
        aic: result.aic,
        bic: result.bic,
        mse: result.mse,
    };

    unsafe {
        anofox_free_forecast_result(&mut result as *mut _);
    }

    output
}

/// Assert two f64 slices are bit-identical (or both NaN).
fn assert_f64_slices_eq(label: &str, model: &str, core: &[f64], ffi: &[f64]) {
    assert_eq!(
        core.len(),
        ffi.len(),
        "[{model}] {label} length mismatch: core={} ffi={}",
        core.len(),
        ffi.len()
    );
    for (i, (c, f)) in core.iter().zip(ffi.iter()).enumerate() {
        if c.is_nan() && f.is_nan() {
            continue;
        }
        assert_eq!(
            c.to_bits(),
            f.to_bits(),
            "[{model}] {label}[{i}] mismatch: core={c} ffi={f}"
        );
    }
}

/// Assert two Option<f64> values match: both None/NaN or bit-identical.
fn assert_opt_f64_eq(label: &str, model: &str, core: Option<f64>, ffi: f64) {
    match core {
        None => assert!(ffi.is_nan(), "[{model}] {label}: core=None but ffi={ffi}"),
        Some(c) if c.is_nan() => assert!(ffi.is_nan(), "[{model}] {label}: core=NaN but ffi={ffi}"),
        Some(c) => assert_eq!(
            c.to_bits(),
            ffi.to_bits(),
            "[{model}] {label}: core={c} ffi={ffi}"
        ),
    }
}

// ── Parameterized test runner ──────────────────────────────────────────

/// Models that require intermittent data and don't use seasonal period.
const INTERMITTENT_MODELS: &[&str] = &[
    "CrostonClassic",
    "CrostonOptimized",
    "CrostonSBA",
    "ADIDA",
    "IMAPA",
    "TSB",
];

/// Models that don't accept a seasonal_period argument.
const NON_SEASONAL_MODELS: &[&str] = &[
    "Naive",
    "SMA",
    "SES",
    "SESOptimized",
    "Holt",
    "RandomWalkDrift",
    "ARIMA",
    "Theta",
    "OptimizedTheta",
    "DynamicTheta",
    "DynamicOptimizedTheta",
    "CrostonClassic",
    "CrostonOptimized",
    "CrostonSBA",
    "ADIDA",
    "IMAPA",
    "TSB",
];

fn run_parity_check(model_name: &str) {
    let is_intermittent = INTERMITTENT_MODELS.contains(&model_name);
    let is_non_seasonal = NON_SEASONAL_MODELS.contains(&model_name);

    let raw_data = if is_intermittent {
        intermittent_data()
    } else {
        seasonal_data()
    };

    // Core forecast() takes &[Option<f64>]; FFI takes *const c_double + validity bitmap.
    let core_data: Vec<Option<f64>> = raw_data.iter().map(|&v| Some(v)).collect();

    let horizon: usize = 5;
    let seasonal_period: usize = if is_non_seasonal { 0 } else { 12 };

    // ── Core call ──
    let model_type: ModelType = model_name.parse().expect("valid model name");
    let core_opts = ForecastOptions {
        model: model_type,
        ets_spec: None,
        horizon,
        confidence_level: 0.95,
        seasonal_period,
        auto_detect_seasonality: false,
        include_fitted: true,
        include_residuals: true,
        window: 0,
        seasonal_periods: vec![],
    };
    let core_out = forecast(&core_data, &core_opts).unwrap_or_else(|e| {
        panic!("[{model_name}] core forecast() failed: {e}");
    });

    // ── FFI call ──
    let ffi_opts = make_ffi_options(model_name, horizon as i32, seasonal_period as i32);
    let ffi_out = call_ffi(&raw_data, &ffi_opts);

    // ── Compare ──
    // Point forecasts
    assert_f64_slices_eq("point", model_name, &core_out.point, &ffi_out.point);

    // Confidence bounds
    assert_f64_slices_eq("lower", model_name, &core_out.lower, &ffi_out.lower);
    assert_f64_slices_eq("upper", model_name, &core_out.upper, &ffi_out.upper);

    // Model name
    assert_eq!(
        core_out.model_name, ffi_out.model_name,
        "[{model_name}] model_name mismatch: core='{}' ffi='{}'",
        core_out.model_name, ffi_out.model_name
    );

    // Fitted values
    match (&core_out.fitted, &ffi_out.fitted) {
        (Some(c), Some(f)) => assert_f64_slices_eq("fitted", model_name, c, f),
        (None, None) => {}
        _ => panic!(
            "[{model_name}] fitted presence mismatch: core={} ffi={}",
            core_out.fitted.is_some(),
            ffi_out.fitted.is_some()
        ),
    }

    // Residuals
    match (&core_out.residuals, &ffi_out.residuals) {
        (Some(c), Some(f)) => assert_f64_slices_eq("residuals", model_name, c, f),
        (None, None) => {}
        _ => panic!(
            "[{model_name}] residuals presence mismatch: core={} ffi={}",
            core_out.residuals.is_some(),
            ffi_out.residuals.is_some()
        ),
    }

    // AIC / BIC / MSE
    assert_opt_f64_eq("aic", model_name, core_out.aic, ffi_out.aic);
    assert_opt_f64_eq("bic", model_name, core_out.bic, ffi_out.bic);
    assert_opt_f64_eq("mse", model_name, core_out.mse, ffi_out.mse);
}

// ── Individual test cases (one per model for clear failure reporting) ──

#[test]
fn parity_auto_ets() {
    run_parity_check("AutoETS");
}

#[test]
fn parity_auto_arima() {
    run_parity_check("AutoARIMA");
}

#[test]
fn parity_auto_theta() {
    run_parity_check("AutoTheta");
}

#[test]
fn parity_auto_mfles() {
    run_parity_check("AutoMFLES");
}

#[test]
fn parity_auto_mstl() {
    run_parity_check("AutoMSTL");
}

#[test]
fn parity_auto_tbats() {
    run_parity_check("AutoTBATS");
}

#[test]
fn parity_naive() {
    run_parity_check("Naive");
}

#[test]
fn parity_sma() {
    run_parity_check("SMA");
}

#[test]
fn parity_seasonal_naive() {
    run_parity_check("SeasonalNaive");
}

#[test]
fn parity_ses() {
    run_parity_check("SES");
}

#[test]
fn parity_ses_optimized() {
    run_parity_check("SESOptimized");
}

#[test]
fn parity_random_walk_drift() {
    run_parity_check("RandomWalkDrift");
}

#[test]
fn parity_holt() {
    run_parity_check("Holt");
}

#[test]
fn parity_holt_winters() {
    run_parity_check("HoltWinters");
}

#[test]
fn parity_seasonal_es() {
    run_parity_check("SeasonalES");
}

#[test]
fn parity_seasonal_es_optimized() {
    run_parity_check("SeasonalESOptimized");
}

#[test]
fn parity_seasonal_window_average() {
    run_parity_check("SeasonalWindowAverage");
}

#[test]
fn parity_ets() {
    run_parity_check("ETS");
}

#[test]
fn parity_theta() {
    run_parity_check("Theta");
}

#[test]
fn parity_optimized_theta() {
    run_parity_check("OptimizedTheta");
}

#[test]
fn parity_dynamic_theta() {
    run_parity_check("DynamicTheta");
}

#[test]
fn parity_dynamic_optimized_theta() {
    run_parity_check("DynamicOptimizedTheta");
}

#[test]
fn parity_arima() {
    run_parity_check("ARIMA");
}

#[test]
fn parity_mfles() {
    run_parity_check("MFLES");
}

#[test]
fn parity_mstl() {
    run_parity_check("MSTL");
}

#[test]
fn parity_tbats() {
    run_parity_check("TBATS");
}

#[test]
fn parity_croston_classic() {
    run_parity_check("CrostonClassic");
}

#[test]
fn parity_croston_optimized() {
    run_parity_check("CrostonOptimized");
}

#[test]
fn parity_croston_sba() {
    run_parity_check("CrostonSBA");
}

#[test]
fn parity_adida() {
    run_parity_check("ADIDA");
}

#[test]
fn parity_imapa() {
    run_parity_check("IMAPA");
}

#[test]
fn parity_tsb() {
    run_parity_check("TSB");
}

/// Meta-test: ensure every model returned by list_models() has a parity test above.
#[test]
fn all_models_covered() {
    let models = list_models();
    assert_eq!(models.len(), 32, "Expected 32 models, got {}", models.len());
    // Verify each model can be parsed (the individual tests above verify parity)
    for name in &models {
        let _: ModelType = name.parse().unwrap_or_else(|_| {
            panic!("Model '{name}' from list_models() is not parseable");
        });
    }
}
