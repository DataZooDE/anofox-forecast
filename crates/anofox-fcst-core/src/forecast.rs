//! Forecasting module wrapping anofox-forecast crate.

use crate::error::{ForecastError, Result};
use crate::imputation::fill_nulls_interpolate;
use crate::seasonality::detect_seasonality;

// Model types from anofox-forecast crate
use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig, ARIMA, SARIMA};
use anofox_forecast::models::baseline::{
    SeasonalNaive as SeasonalNaiveModel, SeasonalWindowAverage as SeasonalWindowAverageModel,
    SimpleMovingAverage,
};
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, ETSSpec, HoltLinearTrend, HoltWinters as HoltWintersModel, ModelPool,
    SeasonalES as SeasonalESModel, SimpleExponentialSmoothing, ETS as ETSModel,
};
use anofox_forecast::models::garch::GARCH;
use anofox_forecast::models::intermittent::{Croston, ADIDA, IMAPA, TSB};
use anofox_forecast::models::kalman_forecaster::KalmanForecaster;
use anofox_forecast::models::laplace::LaplaceForecaster;
use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
use anofox_forecast::models::tbats::{AutoTBATS, TBATS as TBATSModel};
use anofox_forecast::models::theta::{AutoTheta, DynamicTheta, OptimizedTheta, Theta};
use anofox_forecast::models::MFLES;
use anofox_forecast::prelude::Forecaster;

/// Forecast result.
#[derive(Debug, Clone)]
pub struct ForecastOutput {
    /// Point forecasts
    pub point: Vec<f64>,
    /// Lower confidence bounds
    pub lower: Vec<f64>,
    /// Upper confidence bounds
    pub upper: Vec<f64>,
    /// Fitted values (in-sample)
    pub fitted: Option<Vec<f64>>,
    /// Residuals
    pub residuals: Option<Vec<f64>>,
    /// Model name used
    pub model_name: String,
    /// AIC if available
    pub aic: Option<f64>,
    /// BIC if available
    pub bic: Option<f64>,
    /// MSE of in-sample fit
    pub mse: Option<f64>,
}

/// Selector variant for [`ModelType::Laplace`].
///
/// The Laplace forecaster is a streaming distributional shell over
/// EMA / drift / AR(1) / damped-Holt (and optional seasonal) leaves.
/// The variant chooses which zero-config selector is used at fit time.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LaplaceVariant {
    /// `LaplaceForecaster::auto()` — balanced defaults for smooth
    /// economic/continuous series with adequate history.
    #[default]
    Auto,
    /// `LaplaceForecaster::auto_aid()` — AID-based distribution-family
    /// selection; best for retail SKU / intermittent-demand panels.
    AutoAid,
    /// `LaplaceForecaster::skaters()` — the fuller skaters ensemble
    /// (multi-h scoring, stacking, larger leaf set). Slower, more robust.
    Skaters,
}

impl LaplaceVariant {
    /// Parse a variant name (case-insensitive; `auto`, `auto_aid`, `aid`,
    /// `skaters`).
    pub fn parse(s: &str) -> Result<Self> {
        match s.trim().to_lowercase().as_str() {
            "" | "auto" => Ok(LaplaceVariant::Auto),
            "auto_aid" | "autoaid" | "aid" => Ok(LaplaceVariant::AutoAid),
            "skaters" | "skater" => Ok(LaplaceVariant::Skaters),
            other => Err(ForecastError::InvalidParameter {
                param: "laplace_variant".to_string(),
                value: other.to_string(),
                reason: "expected one of: auto, auto_aid, skaters".to_string(),
            }),
        }
    }

    /// Short tag used inside the returned `model_name` string.
    pub fn tag(&self) -> &'static str {
        match self {
            LaplaceVariant::Auto => "auto",
            LaplaceVariant::AutoAid => "auto_aid",
            LaplaceVariant::Skaters => "skaters",
        }
    }
}

/// Available forecast models - matches C++ extension exactly.
/// See: <https://github.com/DataZooDE/anofox-forecast/blob/main/docs/API_REFERENCE.md#supported-models>
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModelType {
    // Automatic Selection Models (6)
    AutoETS,
    AutoARIMA,
    AutoTheta,
    AutoMFLES,
    AutoMSTL,
    AutoTBATS,

    // Basic Models (6)
    Naive,
    SMA,
    SeasonalNaive,
    SES,
    SESOptimized,
    RandomWalkDrift,

    // Exponential Smoothing Models (5)
    Holt,
    HoltWinters,
    SeasonalES,
    SeasonalESOptimized,
    SeasonalWindowAverage,

    // Theta Methods (5) - note: AutoTheta counted above
    Theta,
    OptimizedTheta,
    DynamicTheta,
    DynamicOptimizedTheta,

    // State Space Models (2) - note: AutoETS counted above
    ETS,

    // ARIMA Models (2) - note: AutoARIMA counted above
    ARIMA,

    // Multiple Seasonality Models (6) - note: Auto variants counted above
    MFLES,
    MSTL,
    TBATS,

    // Intermittent Demand Models (6)
    CrostonClassic,
    CrostonOptimized,
    CrostonSBA,
    ADIDA,
    IMAPA,
    TSB,

    // Distributional Models (1) — Laplace shell over EMA/drift/AR(1)/Holt
    // leaves. Variant (Auto / AutoAid / Skaters) is carried in
    // `ForecastOptions.laplace_variant`.
    Laplace,

    // Classical Models (2) — Phase 3 additions
    /// GARCH(p,q) conditional-volatility model. `forecast_value` is the
    /// conditional standard deviation = sqrt(forecast_variance(h)), NOT variance.
    /// Default GARCH(1,1); `garch_p`/`garch_q` in `ForecastOptions` override.
    GARCH,
    /// Kalman filter state-space model. Default spec: local level.
    /// `kalman_model` in `ForecastOptions` selects "local_linear_trend".
    Kalman,
    // Ensemble Models (1) — Phase 4 additions
    /// AutoEnsemble: auto-fits AutoARIMA/AutoETS/AutoTheta, ranks by in-sample MSE,
    /// combines top-K members using the specified CombinationMethod.
    /// `ensemble_top_k` (default 3) and `ensemble_method` (default "mean") in
    /// `ForecastOptions` configure the combination. Prediction intervals are a
    /// passthrough of the crate `AutoEnsemble`'s own `predict_with_intervals`
    /// (D-01); empty/NULL only when the crate itself returns no bounds (D-02).
    AutoEnsemble,
}

impl std::str::FromStr for ModelType {
    type Err = ForecastError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        // First try exact match (case-sensitive, matching C++ extension)
        match s {
            // Automatic Selection Models
            "AutoETS" => return Ok(ModelType::AutoETS),
            "AutoARIMA" => return Ok(ModelType::AutoARIMA),
            "AutoTheta" => return Ok(ModelType::AutoTheta),
            "AutoMFLES" => return Ok(ModelType::AutoMFLES),
            "AutoMSTL" => return Ok(ModelType::AutoMSTL),
            "AutoTBATS" => return Ok(ModelType::AutoTBATS),
            // Basic Models
            "Naive" => return Ok(ModelType::Naive),
            "SMA" => return Ok(ModelType::SMA),
            "SeasonalNaive" => return Ok(ModelType::SeasonalNaive),
            "SES" => return Ok(ModelType::SES),
            "SESOptimized" => return Ok(ModelType::SESOptimized),
            "RandomWalkDrift" | "RandomWalkWithDrift" => return Ok(ModelType::RandomWalkDrift),
            // Exponential Smoothing Models
            "Holt" => return Ok(ModelType::Holt),
            "HoltWinters" => return Ok(ModelType::HoltWinters),
            "SeasonalES" => return Ok(ModelType::SeasonalES),
            "SeasonalESOptimized" => return Ok(ModelType::SeasonalESOptimized),
            "SeasonalWindowAverage" => return Ok(ModelType::SeasonalWindowAverage),
            // Theta Methods
            "Theta" => return Ok(ModelType::Theta),
            "OptimizedTheta" => return Ok(ModelType::OptimizedTheta),
            "DynamicTheta" => return Ok(ModelType::DynamicTheta),
            "DynamicOptimizedTheta" => return Ok(ModelType::DynamicOptimizedTheta),
            // State Space Models
            "ETS" => return Ok(ModelType::ETS),
            // ARIMA Models
            "ARIMA" => return Ok(ModelType::ARIMA),
            // Multiple Seasonality Models
            "MFLES" => return Ok(ModelType::MFLES),
            "MSTL" => return Ok(ModelType::MSTL),
            "TBATS" => return Ok(ModelType::TBATS),
            // Intermittent Demand Models
            "CrostonClassic" => return Ok(ModelType::CrostonClassic),
            "CrostonOptimized" => return Ok(ModelType::CrostonOptimized),
            "CrostonSBA" => return Ok(ModelType::CrostonSBA),
            "ADIDA" => return Ok(ModelType::ADIDA),
            "IMAPA" => return Ok(ModelType::IMAPA),
            "TSB" => return Ok(ModelType::TSB),
            // Distributional
            "Laplace" => return Ok(ModelType::Laplace),
            // Classical
            "GARCH" => return Ok(ModelType::GARCH),
            "Kalman" => return Ok(ModelType::Kalman),
            // Ensemble
            "AutoEnsemble" => return Ok(ModelType::AutoEnsemble),
            _ => {}
        }

        // Fallback: case-insensitive matching for convenience
        match s.to_lowercase().as_str() {
            // Automatic Selection
            "autoets" | "auto_ets" => Ok(ModelType::AutoETS),
            "autoarima" | "auto_arima" => Ok(ModelType::AutoARIMA),
            "autotheta" | "auto_theta" => Ok(ModelType::AutoTheta),
            "automfles" | "auto_mfles" => Ok(ModelType::AutoMFLES),
            "automstl" | "auto_mstl" => Ok(ModelType::AutoMSTL),
            "autotbats" | "auto_tbats" => Ok(ModelType::AutoTBATS),
            // Basic Models
            "naive" => Ok(ModelType::Naive),
            "sma" => Ok(ModelType::SMA),
            "seasonalnaive" | "seasonal_naive" | "snaive" => Ok(ModelType::SeasonalNaive),
            "ses" => Ok(ModelType::SES),
            "sesoptimized" | "ses_optimized" => Ok(ModelType::SESOptimized),
            "randomwalkdrift"
            | "random_walk_drift"
            | "rwd"
            | "drift"
            | "randomwalkwithdrift"
            | "random_walk_with_drift" => Ok(ModelType::RandomWalkDrift),
            // Exponential Smoothing
            "holt" => Ok(ModelType::Holt),
            "holtwinters" | "holt_winters" | "hw" => Ok(ModelType::HoltWinters),
            "seasonales" | "seasonal_es" => Ok(ModelType::SeasonalES),
            "seasonalesoptimized" | "seasonal_es_optimized" => Ok(ModelType::SeasonalESOptimized),
            "seasonalwindowaverage" | "seasonal_window_average" | "swa" => {
                Ok(ModelType::SeasonalWindowAverage)
            }
            // Theta Methods
            "theta" => Ok(ModelType::Theta),
            "optimizedtheta" | "optimized_theta" | "otm" => Ok(ModelType::OptimizedTheta),
            "dynamictheta" | "dynamic_theta" | "dstm" => Ok(ModelType::DynamicTheta),
            "dynamicoptimizedtheta" | "dynamic_optimized_theta" => {
                Ok(ModelType::DynamicOptimizedTheta)
            }
            // State Space
            "ets" => Ok(ModelType::ETS),
            // ARIMA
            "arima" => Ok(ModelType::ARIMA),
            // Multiple Seasonality
            "mfles" => Ok(ModelType::MFLES),
            "mstl" => Ok(ModelType::MSTL),
            "tbats" => Ok(ModelType::TBATS),
            // Intermittent Demand
            "crostonclassic" | "croston_classic" | "croston" => Ok(ModelType::CrostonClassic),
            "crostonoptimized" | "croston_optimized" => Ok(ModelType::CrostonOptimized),
            "crostonsba" | "croston_sba" | "sba" => Ok(ModelType::CrostonSBA),
            "adida" => Ok(ModelType::ADIDA),
            "imapa" => Ok(ModelType::IMAPA),
            "tsb" => Ok(ModelType::TSB),
            // Distributional
            "laplace" => Ok(ModelType::Laplace),
            // Classical
            "garch" => Ok(ModelType::GARCH),
            "kalman" => Ok(ModelType::Kalman),
            // Ensemble
            "autoensemble" | "auto_ensemble" => Ok(ModelType::AutoEnsemble),
            // Auto selection (legacy, maps to AutoETS)
            "auto" => Ok(ModelType::AutoETS),
            _ => Err(ForecastError::InvalidModel(format!("Unknown model: {}", s))),
        }
    }
}

impl ModelType {
    /// Returns the exact model name matching the C++ extension.
    pub fn name(&self) -> &'static str {
        match self {
            // Automatic Selection Models
            ModelType::AutoETS => "AutoETS",
            ModelType::AutoARIMA => "AutoARIMA",
            ModelType::AutoTheta => "AutoTheta",
            ModelType::AutoMFLES => "AutoMFLES",
            ModelType::AutoMSTL => "AutoMSTL",
            ModelType::AutoTBATS => "AutoTBATS",
            // Basic Models
            ModelType::Naive => "Naive",
            ModelType::SMA => "SMA",
            ModelType::SeasonalNaive => "SeasonalNaive",
            ModelType::SES => "SES",
            ModelType::SESOptimized => "SESOptimized",
            ModelType::RandomWalkDrift => "RandomWalkDrift",
            // Exponential Smoothing Models
            ModelType::Holt => "Holt",
            ModelType::HoltWinters => "HoltWinters",
            ModelType::SeasonalES => "SeasonalES",
            ModelType::SeasonalESOptimized => "SeasonalESOptimized",
            ModelType::SeasonalWindowAverage => "SeasonalWindowAverage",
            // Theta Methods
            ModelType::Theta => "Theta",
            ModelType::OptimizedTheta => "OptimizedTheta",
            ModelType::DynamicTheta => "DynamicTheta",
            ModelType::DynamicOptimizedTheta => "DynamicOptimizedTheta",
            // State Space Models
            ModelType::ETS => "ETS",
            // ARIMA Models
            ModelType::ARIMA => "ARIMA",
            // Multiple Seasonality Models
            ModelType::MFLES => "MFLES",
            ModelType::MSTL => "MSTL",
            ModelType::TBATS => "TBATS",
            // Intermittent Demand Models
            ModelType::CrostonClassic => "CrostonClassic",
            ModelType::CrostonOptimized => "CrostonOptimized",
            ModelType::CrostonSBA => "CrostonSBA",
            ModelType::ADIDA => "ADIDA",
            ModelType::IMAPA => "IMAPA",
            ModelType::TSB => "TSB",
            // Distributional
            ModelType::Laplace => "Laplace",
            // Classical
            ModelType::GARCH => "GARCH",
            ModelType::Kalman => "Kalman",
            // Ensemble
            ModelType::AutoEnsemble => "AutoEnsemble",
        }
    }
}

/// Forecast options.
#[derive(Debug, Clone)]
pub struct ForecastOptions {
    /// Model to use
    pub model: ModelType,
    /// ETS model specification (e.g., "AAA", "MNM", "AAdA")
    /// Only used when model is ETS. None means use default (AAA).
    pub ets_spec: Option<String>,
    /// Forecast horizon
    pub horizon: usize,
    /// Confidence level (0-1)
    pub confidence_level: f64,
    /// Seasonal period (0 = auto-detect)
    pub seasonal_period: usize,
    /// Auto-detect seasonality
    pub auto_detect_seasonality: bool,
    /// Include fitted values
    pub include_fitted: bool,
    /// Include residuals
    pub include_residuals: bool,
    /// SMA window size (0 = not set, use period.max(3))
    pub window: usize,
    /// Multiple seasonal periods (empty = not set, use seasonal_period)
    pub seasonal_periods: Vec<usize>,
    /// AutoETS model pool (None = Complete/default)
    pub model_pool: Option<String>,
    /// Laplace forecaster selector (None = Auto). Only consulted when
    /// [`ModelType::Laplace`] is selected.
    pub laplace_variant: Option<LaplaceVariant>,
    /// Enable `LaplaceForecaster::with_seasonal_batch_init()` (opt-in).
    /// Batch-initialises the seasonal-EMA phase levels from the last
    /// training cycle. Safe on stationary or amplitude-declining
    /// seasonal series (large MAE win on the amplitude-decline pathology
    /// tracked at sipemu/anofox-forecast#195). NOT safe on
    /// growing-amplitude or phase-shifted seasonality — the softmax
    /// abandons the seasonal-EMA leaf for a differenced-EMA leaf and
    /// the forecast collapses to flat. Default `false`.
    pub laplace_seasonal_batch_init: bool,
    /// GARCH p order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_p: usize,
    /// GARCH q order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_q: usize,
    /// Kalman state-space spec ("local_level" | "local_linear_trend").
    /// None = "local_level". Only consulted when model is Kalman.
    pub kalman_model: Option<String>,
    /// AutoEnsemble: number of top models to select (0 → default 3).
    /// Only consulted when model is AutoEnsemble.
    pub ensemble_top_k: usize,
    /// AutoEnsemble: combination method string. None → "mean" (Phase 4 default).
    /// Accepted: "" | "mean" | "median" | "weighted_mse" | "inverse_aic" | "stacking" |
    /// "horizon_adaptive" (and common aliases). Only consulted when model is AutoEnsemble.
    pub ensemble_method: Option<String>,
    /// ARIMA non-seasonal order `[p, d, q]`. `None` = unset. Only consulted
    /// when model is `ARIMA`; without it, `ARIMA` is a hard error (D-09) —
    /// use `AutoARIMA` for automatic order selection. See
    /// [`parse_order_triplet`] for the string encoding used at the FFI
    /// boundary.
    pub arima_order: Option<[usize; 3]>,
    /// ARIMA seasonal order `[P, D, Q]`. `None` = unset (non-seasonal).
    /// Only consulted when model is `ARIMA`; requires `seasonal_period > 1`
    /// and a non-zero `arima_order` (D-08/D-09). The seasonal period itself
    /// always comes from `seasonal_period`, never from auto-detection.
    pub seasonal_order: Option<[usize; 3]>,
}

impl Default for ForecastOptions {
    fn default() -> Self {
        Self {
            model: ModelType::AutoETS,
            ets_spec: None,
            horizon: 12,
            confidence_level: 0.95,
            seasonal_period: 0,
            auto_detect_seasonality: true,
            include_fitted: false,
            include_residuals: false,
            window: 0,
            seasonal_periods: vec![],
            model_pool: None,
            laplace_variant: None,
            laplace_seasonal_batch_init: false,
            garch_p: 0,
            garch_q: 0,
            kalman_model: None,
            ensemble_top_k: 0,
            ensemble_method: None,
            arima_order: None,
            seasonal_order: None,
        }
    }
}

/// Exogenous data for forecasting with external regressors.
///
/// Contains both historical regressors (aligned with y values) and
/// future regressors (for the forecast horizon).
#[derive(Debug, Clone, Default)]
pub struct ExogenousData {
    /// Historical regressor values: `historical[regressor_idx][time_idx]`
    /// Each inner Vec must have the same length as the target time series.
    pub historical: Vec<Vec<f64>>,
    /// Future regressor values: `future[regressor_idx][horizon_idx]`
    /// Each inner Vec must have length equal to the forecast horizon.
    pub future: Vec<Vec<f64>>,
}

impl ExogenousData {
    /// Create new exogenous data.
    pub fn new(historical: Vec<Vec<f64>>, future: Vec<Vec<f64>>) -> Self {
        Self { historical, future }
    }

    /// Check if exogenous data is empty.
    pub fn is_empty(&self) -> bool {
        self.historical.is_empty()
    }

    /// Get the number of regressors.
    pub fn n_regressors(&self) -> usize {
        self.historical.len()
    }

    /// Validate that exogenous data dimensions are consistent.
    pub fn validate(&self, n_obs: usize, horizon: usize) -> Result<()> {
        if self.historical.len() != self.future.len() {
            return Err(ForecastError::InvalidInput(format!(
                "Historical has {} regressors but future has {}",
                self.historical.len(),
                self.future.len()
            )));
        }

        for (i, hist) in self.historical.iter().enumerate() {
            if hist.len() != n_obs {
                return Err(ForecastError::InvalidInput(format!(
                    "Regressor {} historical has {} values but expected {}",
                    i,
                    hist.len(),
                    n_obs
                )));
            }
        }

        for (i, fut) in self.future.iter().enumerate() {
            if fut.len() != horizon {
                return Err(ForecastError::InvalidInput(format!(
                    "Regressor {} future has {} values but horizon is {}",
                    i,
                    fut.len(),
                    horizon
                )));
            }
        }

        Ok(())
    }
}

/// Forecast options with exogenous variables support.
#[derive(Debug, Clone)]
pub struct ForecastOptionsExog {
    /// Model to use
    pub model: ModelType,
    /// ETS model specification (e.g., "AAA", "MNM", "AAdA")
    /// Only used when model is ETS. None means use default (AAA).
    pub ets_spec: Option<String>,
    /// Forecast horizon
    pub horizon: usize,
    /// Confidence level (0-1)
    pub confidence_level: f64,
    /// Seasonal period (0 = auto-detect)
    pub seasonal_period: usize,
    /// Auto-detect seasonality
    pub auto_detect_seasonality: bool,
    /// Include fitted values
    pub include_fitted: bool,
    /// Include residuals
    pub include_residuals: bool,
    /// Exogenous data (optional)
    pub exog: Option<ExogenousData>,
    /// SMA window size (0 = not set, use period.max(3))
    pub window: usize,
    /// Multiple seasonal periods (empty = not set, use seasonal_period)
    pub seasonal_periods: Vec<usize>,
    /// AutoETS model pool (None = Complete/default)
    pub model_pool: Option<String>,
    /// Laplace forecaster selector (None = Auto).
    pub laplace_variant: Option<LaplaceVariant>,
    /// Enable `LaplaceForecaster::with_seasonal_batch_init()` (opt-in).
    pub laplace_seasonal_batch_init: bool,
    /// GARCH p order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_p: usize,
    /// GARCH q order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_q: usize,
    /// Kalman state-space spec ("local_level" | "local_linear_trend").
    /// None = "local_level". Only consulted when model is Kalman.
    pub kalman_model: Option<String>,
    /// AutoEnsemble: number of top models to select (0 → default 3).
    /// Only consulted when model is AutoEnsemble.
    pub ensemble_top_k: usize,
    /// AutoEnsemble: combination method string. None → "mean" (Phase 4 default).
    /// Only consulted when model is AutoEnsemble.
    pub ensemble_method: Option<String>,
    /// ARIMA non-seasonal order `[p, d, q]`. See [`ForecastOptions::arima_order`].
    pub arima_order: Option<[usize; 3]>,
    /// ARIMA seasonal order `[P, D, Q]`. See [`ForecastOptions::seasonal_order`].
    pub seasonal_order: Option<[usize; 3]>,
}

impl Default for ForecastOptionsExog {
    fn default() -> Self {
        Self {
            model: ModelType::AutoETS,
            ets_spec: None,
            horizon: 12,
            confidence_level: 0.95,
            seasonal_period: 0,
            auto_detect_seasonality: true,
            include_fitted: false,
            include_residuals: false,
            exog: None,
            window: 0,
            seasonal_periods: vec![],
            model_pool: None,
            laplace_variant: None,
            laplace_seasonal_batch_init: false,
            garch_p: 0,
            garch_q: 0,
            kalman_model: None,
            ensemble_top_k: 0,
            ensemble_method: None,
            arima_order: None,
            seasonal_order: None,
        }
    }
}

impl From<ForecastOptions> for ForecastOptionsExog {
    fn from(opts: ForecastOptions) -> Self {
        Self {
            model: opts.model,
            ets_spec: opts.ets_spec,
            horizon: opts.horizon,
            confidence_level: opts.confidence_level,
            seasonal_period: opts.seasonal_period,
            auto_detect_seasonality: opts.auto_detect_seasonality,
            include_fitted: opts.include_fitted,
            include_residuals: opts.include_residuals,
            exog: None,
            window: opts.window,
            seasonal_periods: opts.seasonal_periods,
            model_pool: opts.model_pool,
            laplace_variant: opts.laplace_variant,
            laplace_seasonal_batch_init: opts.laplace_seasonal_batch_init,
            garch_p: opts.garch_p,
            garch_q: opts.garch_q,
            kalman_model: opts.kalman_model,
            ensemble_top_k: opts.ensemble_top_k,
            ensemble_method: opts.ensemble_method,
            arima_order: opts.arima_order,
            seasonal_order: opts.seasonal_order,
        }
    }
}

/// The reverse of [`From<ForecastOptions> for ForecastOptionsExog`] — drops
/// `exog` only. Used by `forecast_with_exog`'s no-regressor/unsupported-model
/// branch to delegate to [`forecast`] itself (FCST-05 "it runs forecast()
/// itself") so every option (ets_spec, garch_p/q, kalman_model, window,
/// seasonal_periods, model_pool, laplace options, ensemble options, arima
/// orders, confidence level, include flags) is honoured exactly like the
/// non-exog surface, instead of a second hand-maintained dispatch
/// (`forecast_with_model`, deleted) that could silently drift out of sync.
impl From<&ForecastOptionsExog> for ForecastOptions {
    fn from(opts: &ForecastOptionsExog) -> Self {
        Self {
            model: opts.model,
            ets_spec: opts.ets_spec.clone(),
            horizon: opts.horizon,
            confidence_level: opts.confidence_level,
            seasonal_period: opts.seasonal_period,
            auto_detect_seasonality: opts.auto_detect_seasonality,
            include_fitted: opts.include_fitted,
            include_residuals: opts.include_residuals,
            window: opts.window,
            seasonal_periods: opts.seasonal_periods.clone(),
            model_pool: opts.model_pool.clone(),
            laplace_variant: opts.laplace_variant,
            laplace_seasonal_batch_init: opts.laplace_seasonal_batch_init,
            garch_p: opts.garch_p,
            garch_q: opts.garch_q,
            kalman_model: opts.kalman_model.clone(),
            ensemble_top_k: opts.ensemble_top_k,
            ensemble_method: opts.ensemble_method.clone(),
            arima_order: opts.arima_order,
            seasonal_order: opts.seasonal_order,
        }
    }
}

/// Generate forecasts for a time series.
pub fn forecast(values: &[Option<f64>], options: &ForecastOptions) -> Result<ForecastOutput> {
    if !(options.confidence_level > 0.0 && options.confidence_level < 1.0) {
        return Err(ForecastError::InvalidParameter {
            param: "confidence_level".to_string(),
            value: options.confidence_level.to_string(),
            reason: "must be in the open interval (0, 1)".to_string(),
        });
    }

    // Explicit input policy (D-11/D-12): NaN ≡ NULL, ±Inf rejected, all-NULL
    // or <3-point series rejected, leading/trailing NULLs trimmed with
    // trailing-NULL-aware forecast-step alignment handled by finalize_output.
    let prep = prepare_series(values)?;

    // Resolve the seasonal period to use (D-07): explicit > 0 wins; otherwise
    // auto-detect when requested OR the model requires a period; a
    // period-requiring model with nothing detected and no seasonal_periods
    // list is a hard error naming the model — never a silent default.
    let period = resolve_period(
        &prep.values,
        options.model,
        options.seasonal_period,
        options.auto_detect_seasonality,
        !options.seasonal_periods.is_empty(),
    )?;

    // Validate seasonal_period vs model compatibility
    // Only error when user explicitly set seasonal_period (auto_detect_seasonality == false)
    if !options.auto_detect_seasonality && options.seasonal_period > 1 {
        let non_seasonal = matches!(
            options.model,
            ModelType::Naive
                | ModelType::SES
                | ModelType::SESOptimized
                | ModelType::Holt
                | ModelType::RandomWalkDrift
                | ModelType::CrostonClassic
                | ModelType::CrostonOptimized
                | ModelType::CrostonSBA
                | ModelType::TSB
                | ModelType::ADIDA
                | ModelType::IMAPA
        );
        if non_seasonal {
            return Err(ForecastError::InvalidInput(format!(
                "Model '{}' does not use seasonal_period (got {}). \
                 For seasonal forecasting, use: SeasonalNaive, HoltWinters, SeasonalES, AutoETS, AutoMFLES, AutoMSTL, or AutoTBATS.",
                options.model.name(),
                options.seasonal_period
            )));
        }
    }

    // arima_order / seasonal_order (D-08/D-09) are only meaningful for ARIMA;
    // ARIMA validates them itself (incl. seasonal_period interplay) inside
    // forecast_arima — this just rejects them on every other model.
    if options.model != ModelType::ARIMA
        && (options.arima_order.is_some() || options.seasonal_order.is_some())
    {
        let param = if options.arima_order.is_some() {
            "arima_order"
        } else {
            "seasonal_order"
        };
        return Err(ForecastError::InvalidParameter {
            param: param.to_string(),
            value: options.model.name().to_string(),
            reason: "only valid when method='ARIMA'".to_string(),
        });
    }

    // Generate forecast based on model
    // Note: Auto* models (AutoARIMA, AutoETS, etc.) run their respective algorithms
    // with automatic parameter selection, not a generic model selection heuristic
    let level = options.confidence_level;
    // Models are fit on prep.values and asked for horizon + trail_trim steps so
    // finalize_output can drop the lead-in and realign step 1 to the period
    // after the ORIGINAL last row (D-12 trailing-NULL alignment).
    let extended_horizon = options.horizon + prep.trail_trim;
    let mut result = match options.model {
        // Basic Models
        ModelType::Naive => forecast_naive(&prep.values, extended_horizon, level),
        ModelType::SeasonalNaive => {
            forecast_seasonal_naive(&prep.values, extended_horizon, period, level)
        }
        ModelType::SMA => {
            let w = if options.window > 0 {
                options.window
            } else {
                period.max(3)
            };
            forecast_sma(&prep.values, extended_horizon, w, level)
        }
        ModelType::RandomWalkDrift => forecast_drift(&prep.values, extended_horizon, level),
        // Exponential Smoothing
        ModelType::SES => forecast_ses_fixed(&prep.values, extended_horizon, level),
        ModelType::SESOptimized => forecast_ses_optimized(&prep.values, extended_horizon, level),
        ModelType::Holt => forecast_holt_lib(&prep.values, extended_horizon, level),
        ModelType::HoltWinters => {
            forecast_holt_winters_lib(&prep.values, extended_horizon, period, level)
        }
        ModelType::SeasonalES => {
            forecast_seasonal_es_lib(&prep.values, extended_horizon, period, level)
        }
        ModelType::SeasonalESOptimized => {
            forecast_seasonal_es_optimized(&prep.values, extended_horizon, period, level)
        }
        ModelType::SeasonalWindowAverage => {
            forecast_seasonal_window_average(&prep.values, extended_horizon, period, level)
        }
        ModelType::ETS => forecast_ets(
            &prep.values,
            extended_horizon,
            period,
            options.ets_spec.as_deref(),
            level,
        ),
        ModelType::AutoETS => forecast_auto_ets(
            &prep.values,
            extended_horizon,
            period,
            options.model_pool.as_deref(),
            level,
        ),
        // Theta Methods
        ModelType::Theta => forecast_theta_stm(&prep.values, extended_horizon, period, level),
        ModelType::OptimizedTheta => {
            forecast_optimized_theta(&prep.values, extended_horizon, period, level)
        }
        ModelType::DynamicTheta => {
            forecast_dynamic_theta(&prep.values, extended_horizon, period, level)
        }
        ModelType::DynamicOptimizedTheta => {
            forecast_dynamic_optimized_theta(&prep.values, extended_horizon, period, level)
        }
        ModelType::AutoTheta => forecast_auto_theta(&prep.values, extended_horizon, period, level),
        // ARIMA
        ModelType::ARIMA => forecast_arima(
            &prep.values,
            extended_horizon,
            level,
            options.seasonal_period,
            options.arima_order,
            options.seasonal_order,
        ),
        ModelType::AutoARIMA => forecast_auto_arima(&prep.values, extended_horizon, period, level),
        // Multiple Seasonality. MFLES/AutoMFLES both report model_name "MFLES"
        // (D-06 — "Auto" is a documented, deprecated input alias only).
        ModelType::MFLES | ModelType::AutoMFLES => {
            let periods = if !options.seasonal_periods.is_empty() {
                &options.seasonal_periods
            } else if period > 1 {
                &vec![period]
            } else {
                &vec![]
            };
            forecast_mfles(&prep.values, extended_horizon, periods, level).map(|mut r| {
                r.model_name = "MFLES".to_string();
                r
            })
        }
        // MSTL/AutoMSTL both report model_name "MSTL" (D-06 — no AutoMSTL
        // relabel; "Auto" only reflects the alias accepted on input).
        // `resolve_period` already guarantees `period > 1` by the time we
        // get here whenever `seasonal_periods` is empty (D-07) — no hardcoded
        // fallback period is reachable.
        ModelType::MSTL | ModelType::AutoMSTL => {
            let periods = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            forecast_mstl_lib(&prep.values, extended_horizon, &periods, level)
        }
        ModelType::TBATS => {
            let periods = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            forecast_tbats_lib(&prep.values, extended_horizon, &periods, level)
        }
        ModelType::AutoTBATS => {
            let periods = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            forecast_auto_tbats(&prep.values, extended_horizon, &periods, level)
        }
        // Intermittent Demand
        ModelType::CrostonClassic => {
            forecast_croston_classic(&prep.values, extended_horizon, level)
        }
        ModelType::CrostonOptimized => {
            forecast_croston_optimized(&prep.values, extended_horizon, level)
        }
        ModelType::CrostonSBA => forecast_croston_sba(&prep.values, extended_horizon, level),
        ModelType::TSB => forecast_tsb(&prep.values, extended_horizon, level),
        ModelType::ADIDA => forecast_adida(&prep.values, extended_horizon, level),
        ModelType::IMAPA => forecast_imapa(&prep.values, extended_horizon, level),
        // Distributional
        ModelType::Laplace => forecast_laplace(
            &prep.values,
            extended_horizon,
            period,
            options.laplace_variant.unwrap_or_default(),
            options.laplace_seasonal_batch_init,
            options.confidence_level,
        ),
        // Classical
        ModelType::GARCH => forecast_garch(
            &prep.values,
            extended_horizon,
            if options.garch_p == 0 {
                1
            } else {
                options.garch_p
            },
            if options.garch_q == 0 {
                1
            } else {
                options.garch_q
            },
            level,
        ),
        ModelType::Kalman => forecast_kalman(
            &prep.values,
            extended_horizon,
            options.kalman_model.as_deref(),
            level,
        ),
        // Ensemble Models (Phase 4)
        ModelType::AutoEnsemble => forecast_auto_ensemble(
            &prep.values,
            extended_horizon,
            if options.ensemble_top_k == 0 {
                3
            } else {
                options.ensemble_top_k
            },
            options.ensemble_method.as_deref(),
            period,
            level,
        ),
    }?;

    // Use the model_name from the result (contains selected parameters for Auto* models);
    // fall back to the enum name if the result doesn't have a specific one.
    if result.model_name.is_empty() {
        result.model_name = options.model.name().to_string();
    }

    // Prediction intervals, fitted values, residuals, and MSE all come straight from
    // whichever forecast_* function ran above (D-01/D-03/D-11) — never recomputed here.
    // A model whose crate Forecast has no native interval/fitted/residual support simply
    // returns empty/None for those fields (D-02); this wrapper must not paper over that
    // with a fabricated formula. finalize_output (D-12) drops the trail_trim lead-in,
    // realigns to the original date anchor, excludes interpolated points from MSE, and
    // pads fitted/residuals back to the original length.
    Ok(finalize_output(
        result,
        &prep,
        options.horizon,
        options.include_fitted,
        options.include_residuals,
    ))
}

/// Generate forecasts with exogenous variables.
///
/// This function extends the standard `forecast` function to support external
/// regressors (xreg). Exogenous variables can improve forecast accuracy when
/// external factors (e.g., promotions, holidays, weather) influence the target.
///
/// # Supported Models
/// The following models support exogenous variables:
/// - `AutoARIMA`, `ARIMA` (ARIMAX)
/// - `OptimizedTheta`, `DynamicTheta`
/// - `MFLES`
///
/// Other models will ignore the exogenous data and produce a standard forecast.
///
/// # Arguments
/// * `values` - Target time series values (may contain NULLs)
/// * `options` - Forecast options including exogenous data
///
/// # Example
/// ```ignore
/// let y = vec![Some(10.0), Some(20.0), Some(15.0), Some(25.0)];
/// let exog = ExogenousData::new(
///     vec![vec![1.0, 2.0, 1.0, 2.0]],  // historical X
///     vec![vec![1.0, 2.0, 1.0]],        // future X (horizon=3)
/// );
/// let options = ForecastOptionsExog {
///     model: ModelType::AutoARIMA,
///     horizon: 3,
///     exog: Some(exog),
///     ..Default::default()
/// };
/// let result = forecast_with_exog(&y, &options)?;
/// ```
pub fn forecast_with_exog(
    values: &[Option<f64>],
    options: &ForecastOptionsExog,
) -> Result<ForecastOutput> {
    if !(options.confidence_level > 0.0 && options.confidence_level < 1.0) {
        return Err(ForecastError::InvalidParameter {
            param: "confidence_level".to_string(),
            value: options.confidence_level.to_string(),
            reason: "must be in the open interval (0, 1)".to_string(),
        });
    }

    // Validate exogenous data if provided — against the ORIGINAL dimensions,
    // before prepare_series trims/interpolates. Exog arrays are caller-aligned
    // to the raw input; align_exog (below) re-slices them onto the trimmed range.
    if let Some(ref exog) = options.exog {
        exog.validate(values.len(), options.horizon)?;
    }

    // Explicit input policy (D-11/D-12) — see forecast()'s matching comment.
    let prep = prepare_series(values)?;

    // Detect seasonality if needed
    let period = if options.auto_detect_seasonality && options.seasonal_period == 0 {
        detect_seasonality(&prep.values, None)
            .ok()
            .and_then(|p| p.first().cloned())
            .unwrap_or(1) as usize
    } else if options.seasonal_period > 0 {
        options.seasonal_period
    } else {
        1
    };

    // Check if requested model supports exogenous variables
    let supports_exog = matches!(
        options.model,
        ModelType::ARIMA
            | ModelType::AutoARIMA
            | ModelType::OptimizedTheta
            | ModelType::DynamicTheta
            | ModelType::MFLES
            | ModelType::AutoMFLES
    );

    let extended_horizon = options.horizon + prep.trail_trim;
    let aligned_exog = options.exog.as_ref().map(|exog| align_exog(&prep, exog));

    // Generate forecast based on model
    // For models that support exog with exog data provided, use exogenous-aware forecasting
    // Don't do auto-selection when using exog - use the requested model family
    if !(supports_exog && aligned_exog.is_some()) {
        // No exog data, or a model outside the wrapper exog list (native
        // crate exog routing for the rest, D-10, is plan 10-04's scope) —
        // delegate to `forecast()` itself (FCST-05) rather than a second,
        // hand-maintained dispatch (`forecast_with_model`, deleted): every
        // option (ets_spec, garch_p/q, kalman_model, window,
        // seasonal_periods, model_pool, laplace options, ensemble options,
        // arima orders, confidence level, include flags) is honoured
        // exactly like the non-exog surface because it IS that surface.
        // `forecast()` runs its own prepare_series/finalize_output on the
        // ORIGINAL `values`, so this returns directly.
        let opts_without_exog: ForecastOptions = options.into();
        return forecast(values, &opts_without_exog);
    }

    let exog = aligned_exog.as_ref().expect("checked above");
    let mut result = match options.model {
        ModelType::ARIMA | ModelType::AutoARIMA => {
            forecast_arima_with_exog(&prep.values, extended_horizon, exog)
        }
        ModelType::OptimizedTheta | ModelType::DynamicTheta | ModelType::AutoTheta => {
            forecast_theta_with_exog(&prep.values, extended_horizon, exog)
        }
        ModelType::MFLES | ModelType::AutoMFLES => {
            let periods = if !options.seasonal_periods.is_empty() {
                &options.seasonal_periods
            } else if period > 1 {
                &vec![period]
            } else {
                &vec![]
            };
            forecast_mfles_with_exog(&prep.values, extended_horizon, periods, exog)
        }
        _ => {
            // Shouldn't happen due to supports_exog check, but fallback to ARIMA with exog
            forecast_arima_with_exog(&prep.values, extended_horizon, exog)
        }
    }?;

    // Use the model name from the result (e.g., "ARIMAX" for exog) if available,
    // otherwise use the model enum name.
    if result.model_name.is_empty() {
        result.model_name = options.model.name().to_string();
    }

    // Prediction intervals, fitted values, residuals, and MSE all come straight from
    // whichever forecast_*_with_exog function ran above (D-01/D-03/D-11) — never
    // recomputed here. See `forecast()` for the matching non-exog policy, finalize_output,
    // and D-12 trailing-NULL alignment rationale.
    Ok(finalize_output(
        result,
        &prep,
        options.horizon,
        options.include_fitted,
        options.include_residuals,
    ))
}

// Model implementations

fn forecast_naive(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    use anofox_forecast::models::baseline::Naive;
    let ts = make_timeseries(values)?;
    let mut model = Naive::new();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Naive fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "Naive")
}

fn forecast_seasonal_naive(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    // D-07: SeasonalNaive requires an explicit or detected period >= 2 —
    // `resolve_period` (called from `forecast()`) already guarantees this
    // before this function is ever reached in production; a direct call
    // with period < 2 is a hard error (never a silent delegation to Naive,
    // which was this function's pre-10-03 behaviour).
    if period < 2 {
        return Err(ForecastError::InvalidInput(format!(
            "no seasonal period could be detected for model 'SeasonalNaive'; pass \
             seasonal_period explicitly (got {period})"
        )));
    }
    let ts = make_timeseries(values)?;
    let mut model = new_seasonal_naive(period.min(values.len().max(2)));
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("SeasonalNaive fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "SeasonalNaive")
}

fn forecast_sma(
    values: &[f64],
    horizon: usize,
    window: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_sma(window);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("SMA fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "SMA")
}

fn forecast_drift(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    use anofox_forecast::models::baseline::RandomWalkWithDrift;
    let ts = make_timeseries(values)?;
    let mut model = RandomWalkWithDrift::new();
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("RandomWalkDrift fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "RandomWalkDrift")
}

fn forecast_ses_fixed(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = SimpleExponentialSmoothing::new(0.3);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("SES fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "SES")
}

fn forecast_ses_optimized(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = SimpleExponentialSmoothing::auto();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("SESOptimized fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "SESOptimized")
}

fn forecast_holt_lib(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = HoltLinearTrend::auto();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Holt fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "Holt")
}

fn forecast_holt_winters_lib(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_holt_winters(period);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("HoltWinters fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "HoltWinters")
}

fn forecast_theta_stm(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_theta(period);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Theta fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "Theta")
}

fn forecast_optimized_theta(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_optimized_theta(period);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("OptimizedTheta fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "OptimizedTheta")
}

fn forecast_dynamic_theta(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_dynamic_theta(period);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("DynamicTheta fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "DynamicTheta")
}

fn forecast_dynamic_optimized_theta(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_dynamic_optimized_theta(period);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("DynamicOptimizedTheta fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "DynamicOptimizedTheta")
}

fn forecast_seasonal_es_lib(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_seasonal_es(period);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("SeasonalES fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "SeasonalES")
}

fn forecast_seasonal_es_optimized(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_seasonal_es_optimized(period);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("SeasonalESOptimized fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "SeasonalESOptimized")
}

fn forecast_seasonal_window_average(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = new_seasonal_window_average(period, values.len());
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("SeasonalWindowAverage fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "SeasonalWindowAverage")
}

// ============================================================================
// Shared forecaster constructors (FCST-07, D-07): every standalone
// `forecast_*` function above and `build_forecaster`'s matching
// ensemble-member arm call the SAME constructor below — a model built as an
// ensemble member is therefore bit-identical to the standalone forecast for
// that same model name. `requires_seasonal_period` models (see
// `resolve_period`) always receive `period > 1` here in the standalone path;
// `build_forecaster` applies its own defensive guard (D-07) for the
// ensemble-member path, where a per-member period isn't individually
// detected.
// ============================================================================

fn new_sma(window: usize) -> SimpleMovingAverage {
    SimpleMovingAverage::new(window)
}

fn new_holt_winters(period: usize) -> HoltWintersModel {
    HoltWintersModel::auto(
        period,
        anofox_forecast::models::exponential::SeasonalType::Additive,
    )
}

fn new_seasonal_es(period: usize) -> SeasonalESModel {
    SeasonalESModel::new(period)
}

fn new_seasonal_es_optimized(period: usize) -> SeasonalESModel {
    SeasonalESModel::optimized(period)
}

fn new_seasonal_naive(period: usize) -> SeasonalNaiveModel {
    SeasonalNaiveModel::new(period)
}

/// `n_seasons = (n_obs / period).max(1)` — mirrors the adaptive computation
/// the standalone path has always used (`values.len() / p`), now shared
/// with `build_forecaster` via the `n_obs` parameter threaded through its
/// signature (D-07; previously ensemble members hard-coded `n_seasons=2`).
fn new_seasonal_window_average(period: usize, n_obs: usize) -> SeasonalWindowAverageModel {
    let p = period.max(1);
    let n_seasons = (n_obs / p).max(1);
    SeasonalWindowAverageModel::new(p, n_seasons)
}

fn new_theta(period: usize) -> Theta {
    if period > 1 {
        Theta::seasonal(period)
    } else {
        Theta::new()
    }
}

fn new_optimized_theta(period: usize) -> OptimizedTheta {
    if period > 1 {
        OptimizedTheta::seasonal(period)
    } else {
        OptimizedTheta::new()
    }
}

fn new_dynamic_theta(period: usize) -> DynamicTheta {
    if period > 1 {
        DynamicTheta::seasonal(period)
    } else {
        DynamicTheta::new(0.1)
    }
}

fn new_dynamic_optimized_theta(period: usize) -> DynamicTheta {
    if period > 1 {
        DynamicTheta::seasonal_optimized(period)
    } else {
        DynamicTheta::optimized()
    }
}

fn new_auto_theta(period: usize) -> AutoTheta {
    if period > 1 {
        AutoTheta::seasonal(period)
    } else {
        AutoTheta::new()
    }
}

fn new_auto_arima(period: usize) -> AutoARIMA {
    let cfg = if period > 1 {
        AutoARIMAConfig::default().with_seasonal_period(period)
    } else {
        AutoARIMAConfig::default()
    };
    AutoARIMA::with_config(cfg)
}

/// Kalman filter state-space specification (D-07): the only two crate specs
/// `KalmanForecaster` exposes. `None`/`""` means `LocalLevel` (the
/// documented default) — anything else unrecognised is a hard
/// `InvalidParameter` naming `kalman_model`, never a silent default.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KalmanSpec {
    /// `KalmanForecaster::local_level()`.
    LocalLevel,
    /// `KalmanForecaster::local_linear_trend()`.
    LocalLinearTrend,
}

impl KalmanSpec {
    /// Tag used inside the `"Kalman(<tag>)"` model_name.
    pub fn tag(&self) -> &'static str {
        match self {
            KalmanSpec::LocalLevel => "local_level",
            KalmanSpec::LocalLinearTrend => "local_linear_trend",
        }
    }
}

/// Parse a `kalman_model` option string (D-07). `None`/`""`/`"local_level"`
/// (case-insensitive) → `LocalLevel`; `"local_linear_trend"` →
/// `LocalLinearTrend`; anything else is `InvalidParameter { param:
/// "kalman_model" }` — never a silent default.
pub fn parse_kalman_spec(s: Option<&str>) -> Result<KalmanSpec> {
    match s.unwrap_or("").trim().to_lowercase().as_str() {
        "" | "local_level" => Ok(KalmanSpec::LocalLevel),
        "local_linear_trend" => Ok(KalmanSpec::LocalLinearTrend),
        other => Err(ForecastError::InvalidParameter {
            param: "kalman_model".to_string(),
            value: other.to_string(),
            reason: "expected local_level or local_linear_trend".to_string(),
        }),
    }
}

fn new_kalman(spec: KalmanSpec) -> KalmanForecaster {
    match spec {
        KalmanSpec::LocalLevel => KalmanForecaster::local_level(),
        KalmanSpec::LocalLinearTrend => KalmanForecaster::local_linear_trend(),
    }
}

/// Models that require a usable seasonal period to fit meaningfully (D-07).
/// `resolve_period` uses this to decide whether auto-detection is forced and
/// whether "no period available" is a hard error rather than a silent
/// default of 12 (or 2).
fn requires_seasonal_period(model: ModelType) -> bool {
    matches!(
        model,
        ModelType::HoltWinters
            | ModelType::SeasonalES
            | ModelType::SeasonalESOptimized
            | ModelType::SeasonalNaive
            | ModelType::SeasonalWindowAverage
            | ModelType::MSTL
            | ModelType::AutoMSTL
            | ModelType::TBATS
            | ModelType::AutoTBATS
    )
}

/// Resolve the seasonal period to use for `model` (D-07). An explicit
/// `seasonal_period > 0` always wins. Otherwise `detect_seasonality` runs
/// when `auto_detect` is true OR `model` is in [`requires_seasonal_period`]
/// (treating `Err`/empty as "none detected"). If `model` requires a period,
/// has no `seasonal_periods` list (`has_period_list`), and no period was
/// detected (`<= 1`), this is a hard `InvalidInput` naming the model —
/// never a silent default of 12 or 2.
fn resolve_period(
    values: &[f64],
    model: ModelType,
    seasonal_period: usize,
    auto_detect: bool,
    has_period_list: bool,
) -> Result<usize> {
    if seasonal_period > 0 {
        return Ok(seasonal_period);
    }

    let needs_detection = auto_detect || requires_seasonal_period(model);
    let detected = if needs_detection {
        detect_seasonality(values, None)
            .ok()
            .and_then(|p| p.first().copied())
            .map(|p| p as usize)
            .unwrap_or(0)
    } else {
        0
    };

    if requires_seasonal_period(model) && !has_period_list && detected <= 1 {
        return Err(ForecastError::InvalidInput(format!(
            "no seasonal period could be detected for model '{}'; pass seasonal_period \
             (or seasonal_periods) explicitly",
            model.name()
        )));
    }

    Ok(detected.max(1))
}

/// Validate ETS notation format.
/// Valid ETS notations follow the pattern: [E][T][S] or [E][Td][S]
/// where E = Error (A or M), T = Trend (A, M, or N), S = Seasonal (A, M, or N)
/// and 'd' indicates damped trend.
/// Examples: AAA, MNM, AAdA, MAdM
fn is_valid_ets_notation(notation: &str) -> bool {
    let chars: Vec<char> = notation.chars().collect();

    match chars.len() {
        // 3-character notation: ETS (e.g., AAA, MNM)
        3 => {
            let valid_error = chars[0] == 'A' || chars[0] == 'M';
            let valid_trend = chars[1] == 'A' || chars[1] == 'M' || chars[1] == 'N';
            let valid_seasonal = chars[2] == 'A' || chars[2] == 'M' || chars[2] == 'N';
            valid_error && valid_trend && valid_seasonal
        }
        // 4-character notation: ETdS with damped trend (e.g., AAdA, MAdM)
        4 => {
            let valid_error = chars[0] == 'A' || chars[0] == 'M';
            let valid_trend = chars[1] == 'A' || chars[1] == 'M';
            let valid_damped = chars[2] == 'd';
            let valid_seasonal = chars[3] == 'A' || chars[3] == 'M' || chars[3] == 'N';
            valid_error && valid_trend && valid_damped && valid_seasonal
        }
        _ => false,
    }
}

fn forecast_ets(
    values: &[f64],
    horizon: usize,
    period: usize,
    ets_spec: Option<&str>,
    level: f64,
) -> Result<ForecastOutput> {
    // Parse and validate ETS specification if provided
    if let Some(notation) = ets_spec {
        // Validate the notation format
        if !is_valid_ets_notation(notation) {
            return Err(ForecastError::InvalidInput(format!(
                "Invalid ETS model specification '{}'. \
                 Expected 3 or 4 character notation: Error(A/M) + Trend(A/M/N) + Seasonal(A/M/N), \
                 with optional 'd' for damped trend. \
                 Examples: 'AAA' (additive), 'MNM' (multiplicative error, no trend), \
                 'AAdA' (additive damped trend). \
                 Valid characters: A=Additive, M=Multiplicative, N=None, d=Damped.",
                notation
            )));
        }

        // Parse the spec
        let parsed_spec = ETSSpec::from_notation(notation).map_err(|e| {
            ForecastError::InvalidInput(format!(
                "Invalid ETS model specification '{}': {}",
                notation, e
            ))
        })?;

        // Reject unstable combinations (multiplicative error with additive components)
        if !parsed_spec.is_valid() {
            return Err(ForecastError::InvalidInput(format!(
                "ETS model '{}' is an unstable combination (multiplicative error with additive components). \
                 Try one of: 'AAA', 'ANA', 'AAdA', 'MNM', 'MAM', 'MAdM', 'MMM', 'MMdM', or use 'AutoETS' for automatic selection.",
                notation
            )));
        }

        // User explicitly requested this spec — if it fails to fit,
        // return ComputationError so the group is skipped (null forecast),
        // not an InvalidInput error that would abort the whole pipeline.
        return forecast_with_ets_spec(values, horizon, period, &parsed_spec, level).map_err(|e| {
            ForecastError::ComputationError(format!(
                "ETS model '{}' failed to fit: {}",
                notation, e
            ))
        });
    }

    // No explicit spec: 'ETS' means the same crate AutoETS selection as
    // 'AutoETS' (D-05) — the chosen spec is reported un-prefixed in
    // model_name (e.g. "ETS(M,A,M)"), never "ETS(ETS(...))" and never a
    // hand-picked HoltWinters/Holt/SES substitute.
    forecast_auto_ets_core(values, horizon, period, None, level, false)
}

/// Forecast using the anofox-forecast ETS model with explicit spec.
fn forecast_with_ets_spec(
    values: &[f64],
    horizon: usize,
    period: usize,
    spec: &ETSSpec,
    level: f64,
) -> Result<ForecastOutput> {
    // Determine seasonal period for the model
    let seasonal_period = if spec.has_seasonal() && period > 1 {
        period
    } else {
        1
    };

    // Create TimeSeries from values (must include timestamps)
    let time_series = make_timeseries(values)?;

    // Create the ETS forecaster
    let mut forecaster = ETSModel::new(*spec, seasonal_period);

    // Fit the model
    forecaster
        .fit(&time_series)
        .map_err(|e| ForecastError::ComputationError(format!("Failed to fit ETS model: {}", e)))?;

    // Generate forecasts (D-01: native intervals, not point-only)
    let forecast = forecaster
        .predict_with_intervals(horizon, level)
        .map_err(|e| {
            ForecastError::ComputationError(format!("Failed to generate ETS forecasts: {}", e))
        })?;

    // Extract point forecasts (univariate - first dimension)
    let point = forecast.point().first().cloned().unwrap_or_default();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    // Get fitted/residuals from the crate (D-03/D-11)
    let fitted = forecaster.fitted_values().map(|v| v.to_vec());
    let residuals = forecaster.residuals().map(|v| v.to_vec());

    // Get model name from spec. `ETSSpec::short_name()` already returns the
    // fully-formed "ETS(E,T,S)" text (anofox-forecast 0.15.10) — wrapping it
    // again produced the "ETS(ETS(M,A,M))" double-wrap bug fixed by D-05.
    let model_name = spec.short_name();

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted,
        residuals,
        model_name,
        aic: None,
        bic: None,
        mse: None,
    })
}

/// Parse an `arima_order`/`seasonal_order` string like `"[0,1,1]"` or
/// `" 0, 1 ,1 "` into `Option<[usize; 3]>` (D-08/D-09).
///
/// An empty (post-trim) string is `Ok(None)` ("unset"). Otherwise: one
/// optional leading `[`/`(` and trailing `]`/`)` are stripped, the remainder
/// is split on `,`, and it must yield exactly three non-negative decimal
/// integers. AR/MA orders (`p`, `q` — positions 0 and 2) must be `<= 10`;
/// the differencing order (`d` — position 1) must be `<= 2`. Any violation
/// (wrong count, non-digit characters, negative sign, decimal point,
/// out-of-range value) is `InvalidParameter` naming `param` and echoing the
/// original (untrimmed) `s` as `value` — never a silent default or a
/// partially-parsed result.
pub fn parse_order_triplet(param: &str, s: &str) -> Result<Option<[usize; 3]>> {
    let trimmed = s.trim();
    if trimmed.is_empty() {
        return Ok(None);
    }

    let invalid = || ForecastError::InvalidParameter {
        param: param.to_string(),
        value: s.to_string(),
        reason: "expected three non-negative integers [p,d,q] with p,q <= 10 and d <= 2"
            .to_string(),
    };

    let mut inner = trimmed;
    if let Some(rest) = inner.strip_prefix('[').or_else(|| inner.strip_prefix('(')) {
        inner = rest;
    }
    if let Some(rest) = inner.strip_suffix(']').or_else(|| inner.strip_suffix(')')) {
        inner = rest;
    }

    let parts: Vec<&str> = inner.split(',').collect();
    if parts.len() != 3 {
        return Err(invalid());
    }

    let mut vals = [0usize; 3];
    for (i, part) in parts.iter().enumerate() {
        let p = part.trim();
        if p.is_empty() || !p.chars().all(|c| c.is_ascii_digit()) {
            return Err(invalid());
        }
        vals[i] = p.parse::<usize>().map_err(|_| invalid())?;
    }

    let (p, d, q) = (vals[0], vals[1], vals[2]);
    if p > 10 || q > 10 || d > 2 {
        return Err(invalid());
    }

    Ok(Some([p, d, q]))
}

/// Parse a `seasonal_periods` string like `"[7, 365]"` into `Vec<usize>`
/// (D-07 — the strict replacement for the FFI's old silently-dropping
/// `parse_seasonal_periods_str`).
///
/// An empty (post-trim) string is `Ok(vec![])`. Otherwise: one optional
/// leading `[`/`(` and trailing `]`/`)` are stripped, the remainder is split
/// on `,`, and every entry must parse as a non-negative decimal integer
/// `>= 2` — any non-digit entry or any value `< 2` (including `0`/`1`,
/// which cannot be a seasonal period) is `InvalidParameter { param:
/// "seasonal_periods" }`; no entry is ever silently dropped.
pub fn parse_seasonal_periods(s: &str) -> Result<Vec<usize>> {
    let trimmed = s.trim();
    if trimmed.is_empty() {
        return Ok(vec![]);
    }

    let invalid = || ForecastError::InvalidParameter {
        param: "seasonal_periods".to_string(),
        value: s.to_string(),
        reason: "expected a comma-separated list of integers >= 2, e.g. \"[7, 365]\"".to_string(),
    };

    let mut inner = trimmed;
    if let Some(rest) = inner.strip_prefix('[').or_else(|| inner.strip_prefix('(')) {
        inner = rest;
    }
    if let Some(rest) = inner.strip_suffix(']').or_else(|| inner.strip_suffix(')')) {
        inner = rest;
    }

    let mut out = Vec::new();
    for part in inner.split(',') {
        let p = part.trim();
        if p.is_empty() || !p.chars().all(|c| c.is_ascii_digit()) {
            return Err(invalid());
        }
        let v: usize = p.parse().map_err(|_| invalid())?;
        if v < 2 {
            return Err(invalid());
        }
        out.push(v);
    }

    Ok(out)
}

/// Real crate ARIMA/SARIMA fit (D-08/D-09). `arima_order=[p,d,q]` is mandatory
/// — `ARIMA` without it is a hard error naming `AutoARIMA` as the
/// automatic-order alternative, never a silent fallback (the old fixed-AR(1)
/// stand-in this replaces, and the `n<5`-delegates-to-Naive branch it had,
/// are both gone entirely).
///
/// `seasonal_order=[P,D,Q]` is optional; a non-zero seasonal order requires
/// an explicit `seasonal_period > 1` (never the auto-detected period — the
/// caller passes `options.seasonal_period` directly, see `forecast()`).
/// `seasonal_order=[0,0,0]` (or omitted) is non-seasonal: a plain `ARIMA`
/// fit runs even if the caller passed an all-zero seasonal order.
fn forecast_arima(
    values: &[f64],
    horizon: usize,
    level: f64,
    seasonal_period: usize,
    arima_order: Option<[usize; 3]>,
    seasonal_order: Option<[usize; 3]>,
) -> Result<ForecastOutput> {
    let Some([p, d, q]) = arima_order else {
        return Err(ForecastError::InvalidInput(
            "ARIMA requires arima_order=[p,d,q] (e.g. [1,1,1]); use 'AutoARIMA' for \
             automatic order selection"
                .to_string(),
        ));
    };

    // seasonal_order=[0,0,0] is explicitly non-seasonal (D-08) — only a
    // genuinely non-zero seasonal order engages SARIMA / requires a period.
    let seasonal_nonzero = seasonal_order
        .map(|[cap_p, cap_d, cap_q]| cap_p + cap_d + cap_q > 0)
        .unwrap_or(false);

    if seasonal_nonzero && seasonal_period <= 1 {
        return Err(ForecastError::InvalidInput(
            "seasonal_order requires seasonal_period > 1".to_string(),
        ));
    }
    if !seasonal_nonzero && seasonal_period > 1 {
        return Err(ForecastError::InvalidInput(
            "seasonal_period > 1 requires a non-zero seasonal_order=[P,D,Q] (e.g. [0,1,1]); \
             ARIMA never uses the auto-detected seasonal period"
                .to_string(),
        ));
    }

    let ts = make_timeseries(values)?;

    // Constant handling (per the crate's documented allowdrift-style AICc
    // comparison): d+D == 1 with p or q >= 1 may fit a drift term; d+D == 0
    // optimises an intercept (≈ mean); d+D >= 2 never carries a constant.
    // Report whichever non-zero constant the crate actually fit.
    let output = if seasonal_nonzero {
        let [cap_p, cap_d, cap_q] = seasonal_order.expect("seasonal_nonzero implies Some");
        let s = seasonal_period;
        let name = format!("ARIMA({p},{d},{q})({cap_p},{cap_d},{cap_q})[{s}]");
        let mut model = SARIMA::new(p, d, q, cap_p, cap_d, cap_q, s);
        model
            .fit(&ts)
            .map_err(|e| ForecastError::ComputationError(format!("{name} fit failed: {e}")))?;
        let intercept = model.intercept();
        let mut out = extract_forecast(&model, horizon, level, &name)?;
        let d_total = d + cap_d;
        if d_total == 1 && intercept != 0.0 {
            out.model_name.push_str(" with drift");
        } else if d_total == 0 && intercept != 0.0 {
            out.model_name.push_str(" with non-zero mean");
        }
        out
    } else {
        let name = format!("ARIMA({p},{d},{q})");
        let mut model = ARIMA::new(p, d, q);
        model
            .fit(&ts)
            .map_err(|e| ForecastError::ComputationError(format!("{name} fit failed: {e}")))?;
        let intercept = model.intercept();
        let mut out = extract_forecast(&model, horizon, level, &name)?;
        if d == 1 && intercept != 0.0 {
            out.model_name.push_str(" with drift");
        } else if d == 0 && intercept != 0.0 {
            out.model_name.push_str(" with non-zero mean");
        }
        out
    };

    Ok(output)
}

/// AutoARIMA: Automatic ARIMA model selection using AIC-based search.
/// Uses the proper AutoARIMA implementation from anofox-forecast library.
fn forecast_auto_arima(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    use chrono::{Duration, TimeZone, Utc};

    // Create timestamps for TimeSeries (required by anofox-forecast)
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();

    let ts = TimeSeries::univariate(timestamps, values.to_vec()).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to create TimeSeries: {}", e))
    })?;

    let mut model = new_auto_arima(period);

    // Fit the model
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("AutoARIMA fit failed: {}", e)))?;

    // Get forecasts (D-01: native intervals)
    let forecast = model
        .predict_with_intervals(horizon, level)
        .map_err(|e| ForecastError::ComputationError(format!("AutoARIMA predict failed: {}", e)))?;

    // Get the selected order for model name
    // The model_scores() contains sorted (best first) results
    let model_name = if let Some(order) = model.selected_full_order() {
        let name = if order.is_seasonal() {
            format!(
                "AutoARIMA({},{},{})({},{},{})[{}]",
                order.p, order.d, order.q, order.cap_p, order.cap_d, order.cap_q, order.s
            )
        } else {
            format!("AutoARIMA({},{},{})", order.p, order.d, order.q)
        };
        name
    } else if let Some((p, d, q)) = model.selected_order() {
        format!("AutoARIMA({},{},{})", p, d, q)
    } else if let Some((order, _score)) = model.model_scores().first() {
        // Fallback: use best model from scores
        if order.is_seasonal() {
            format!(
                "AutoARIMA({},{},{})({},{},{})[{}]",
                order.p, order.d, order.q, order.cap_p, order.cap_d, order.cap_q, order.s
            )
        } else {
            format!("AutoARIMA({},{},{})", order.p, order.d, order.q)
        }
    } else {
        "AutoARIMA".to_string()
    };

    // Extract point forecasts (primary dimension)
    let point = forecast.primary().to_vec();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        model_name,
        aic: None,
        bic: None,
        mse: None,
    })
}

/// Parse a model pool string into the `ModelPool` enum.
fn parse_model_pool(s: &str) -> Result<ModelPool> {
    match s.to_lowercase().replace(['-', '_'], "").as_str() {
        "complete" => Ok(ModelPool::Complete),
        "nomultiplicativetrend" => Ok(ModelPool::NoMultiplicativeTrend),
        "dampedtrendonly" => Ok(ModelPool::DampedTrendOnly),
        "matcherrorseasonal" => Ok(ModelPool::MatchErrorSeasonal),
        "reduced" => Ok(ModelPool::Reduced),
        _ => Err(ForecastError::InvalidInput(format!(
            "Unknown model_pool '{}'. Valid options: complete, no_multiplicative_trend, \
             damped_trend_only, match_error_seasonal, reduced",
            s
        ))),
    }
}

/// Build a crate `AutoETS` configured consistently for every call site that
/// means "run the crate's own AICc model search": standalone `AutoETS`,
/// `ETS` without an explicit spec (D-05), and ensemble members built via
/// `build_forecaster` — the single shared constructor that makes all three
/// bit-identical by construction (FCST-07).
fn new_auto_ets(period: usize, pool: Option<ModelPool>) -> AutoETS {
    let mut config = if period > 1 {
        AutoETSConfig::with_period(period)
    } else {
        AutoETSConfig::non_seasonal()
    };
    if let Some(p) = pool {
        config = config.with_model_pool(p);
    }
    AutoETS::with_config(config)
}

/// Format an AutoETS-selected spec's `model_name`. `ETSSpec::short_name()`
/// already returns the fully-formed `"ETS(E,T,S)"` text (e.g. "ETS(M,A,M)");
/// `auto=false` ('ETS' without a spec, D-05) returns that text unchanged —
/// identical to an explicit `ETS` spec's model_name, never double-wrapped —
/// while `auto=true` (standalone 'AutoETS') prepends "Auto" to get
/// "AutoETS(M,A,M)".
fn format_auto_ets_name(spec: &ETSSpec, auto: bool) -> String {
    let short = spec.short_name();
    if auto {
        format!("Auto{short}")
    } else {
        short
    }
}

/// AutoETS: Automatic ETS model selection using AICc-based search.
/// Uses the proper AutoETS implementation from anofox-forecast library.
/// A crate fit/predict error or panic (e.g. a constant series causing NaN
/// in the optimizer — issue #192) is a hard `InvalidInput` naming AutoETS
/// (D-07) — never a silent fallback to a different model.
fn forecast_auto_ets(
    values: &[f64],
    horizon: usize,
    period: usize,
    model_pool: Option<&str>,
    level: f64,
) -> Result<ForecastOutput> {
    forecast_auto_ets_core(values, horizon, period, model_pool, level, true)
}

/// Shared AutoETS core (D-05/FCST-07). `auto_label=true` is standalone
/// 'AutoETS' (model_name "AutoETS(...)"); `auto_label=false` is 'ETS'
/// without an explicit spec (model_name "ETS(...)", via `forecast_ets`) —
/// both run the exact same crate search through [`new_auto_ets`].
fn forecast_auto_ets_core(
    values: &[f64],
    horizon: usize,
    period: usize,
    model_pool: Option<&str>,
    level: f64,
    auto_label: bool,
) -> Result<ForecastOutput> {
    use std::panic::{catch_unwind, AssertUnwindSafe};

    // Parse model_pool before entering catch_unwind (error reporting is cleaner)
    let pool = match model_pool {
        Some(s) => Some(parse_model_pool(s)?),
        None => None,
    };

    let lib_result = catch_unwind(AssertUnwindSafe(|| -> Result<ForecastOutput> {
        let ts = make_timeseries(values)?;
        let mut model = new_auto_ets(period, pool);

        // Fit the model
        model
            .fit(&ts)
            .map_err(|e| ForecastError::ComputationError(format!("AutoETS fit failed: {}", e)))?;

        // Get forecasts (D-01: native intervals)
        let forecast = model.predict_with_intervals(horizon, level).map_err(|e| {
            ForecastError::ComputationError(format!("AutoETS predict failed: {}", e))
        })?;

        // Get the selected spec for model name. A successful fit with no
        // selected spec would otherwise surface as an unlabelled "AutoETS"/
        // "ETS" result — report it as an error naming AutoETS instead (D-07).
        let model_name = match model.selected_spec() {
            Some(spec) => format_auto_ets_name(&spec, auto_label),
            None => {
                return Err(ForecastError::InvalidInput(
                    "AutoETS fit succeeded but reported no selected spec".to_string(),
                ))
            }
        };

        // Extract point forecasts (primary dimension)
        let point = forecast.primary().to_vec();
        let (lower, upper) = extract_bounds(&forecast, point.len());

        Ok(ForecastOutput {
            point,
            lower,
            upper,
            fitted: model.fitted_values().map(|v| v.to_vec()),
            residuals: model.residuals().map(|v| v.to_vec()),
            model_name,
            aic: None,
            bic: None,
            mse: None,
        })
    }));

    match lib_result {
        Ok(Ok(output)) => Ok(output),
        Ok(Err(e)) => Err(ForecastError::InvalidInput(format!(
            "AutoETS could not fit this series: {e}. Constant or near-constant series are \
             not supported by AutoETS; use 'Naive' or 'SES' for them"
        ))),
        Err(_) => Err(ForecastError::InvalidInput(
            "AutoETS could not fit this series: crate panic. Constant or near-constant \
             series are not supported by AutoETS; use 'Naive' or 'SES' for them"
                .to_string(),
        )),
    }
}

/// Laplace: streaming distributional shell over EMA / drift / AR(1) / damped-Holt
/// (and optional seasonal) leaves. Returns a point + interval forecast; the
/// full mixture parameters are surfaced by the `ts_forecast_dist_by` table
/// function (PR B of the distributional series).
fn forecast_laplace(
    values: &[f64],
    horizon: usize,
    period: usize,
    variant: LaplaceVariant,
    seasonal_batch_init: bool,
    confidence_level: f64,
) -> Result<ForecastOutput> {
    use std::panic::{catch_unwind, AssertUnwindSafe};

    let lib_result = catch_unwind(AssertUnwindSafe(|| -> Result<ForecastOutput> {
        let ts = make_timeseries(values)?;

        let mut model = match variant {
            LaplaceVariant::Auto => LaplaceForecaster::new().auto(),
            LaplaceVariant::AutoAid => LaplaceForecaster::new().auto_aid(),
            LaplaceVariant::Skaters => LaplaceForecaster::new().skaters(),
        };
        if period > 1 {
            model = model.with_seasonal(period);
        }
        if seasonal_batch_init && period > 1 {
            // Opt-in per anofox-forecast 0.15.1 guidance: safe on stationary
            // or declining amplitude, unsafe on growing amplitude / phase-shifted
            // seasonality. See sipemu/anofox-forecast#195.
            model = model.with_seasonal_batch_init();
        }

        model
            .fit(&ts)
            .map_err(|e| ForecastError::ComputationError(format!("Laplace fit failed: {e}")))?;

        // predict_with_intervals uses a symmetric two-sided level; clamp to
        // [0.5, 0.999] to stay within the LaplaceForecaster's supported range.
        let level = confidence_level.clamp(0.5, 0.999);
        let forecast = model
            .predict_with_intervals(horizon, level)
            .map_err(|e| ForecastError::ComputationError(format!("Laplace predict failed: {e}")))?;

        let point = forecast.primary().to_vec();
        let (lower, upper) = extract_bounds(&forecast, point.len());

        // Fitted/residuals from the crate (D-03/D-11) — no SES(0.3) approximation.
        let fitted = model.fitted_values().map(|v| v.to_vec());
        let residuals = model.residuals().map(|v| v.to_vec());

        let model_name = match (period > 1, seasonal_batch_init && period > 1) {
            (true, true) => format!("Laplace({},seasonal={},batch_init)", variant.tag(), period),
            (true, false) => format!("Laplace({},seasonal={})", variant.tag(), period),
            (false, _) => format!("Laplace({})", variant.tag()),
        };

        Ok(ForecastOutput {
            point,
            lower,
            upper,
            fitted,
            residuals,
            model_name,
            aic: None,
            bic: None,
            mse: None,
        })
    }));

    match lib_result {
        Ok(Ok(output)) => Ok(output),
        Ok(Err(e)) => Err(e),
        Err(_) => Err(ForecastError::ComputationError(
            "Laplace forecast panicked (likely constant or near-empty series)".to_string(),
        )),
    }
}

/// Fit-state inspection via the crate's `Inspectable` trait.
///
/// Fits the requested model, calls `.explanation()`, and returns the
/// per-family `Explanation` variant as a JSON string. C++ side unpacks
/// the JSON into a wide STRUCT via `struct_pack + json_extract` in
/// `ts_forecast_inspect_by`.
///
/// Models covered (Inspectable in anofox-forecast 0.15.3):
/// - AutoETS, AutoARIMA, AutoTheta, AutoTBATS (auto-selection family)
/// - MFLES, MSTL (multi-seasonal)
/// - LaplaceForecaster (distributional)
///
/// Unsupported models return an `InvalidModel` error naming the model.
pub fn forecast_inspect(values: &[Option<f64>], options: &ForecastOptions) -> Result<String> {
    use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
    use anofox_forecast::models::tbats::AutoTBATS;
    use anofox_forecast::models::{Inspectable, MFLES};
    use anofox_forecast::prelude::Forecaster;

    // Explicit input policy (D-11/D-12) — see forecast()'s matching comment.
    let prep = prepare_series(values)?;

    // D-07: resolve_period errors for a period-requiring model (AutoTBATS/
    // AutoMSTL here) with nothing detected and no seasonal_periods list —
    // never a silent hardcoded-period default.
    let period = resolve_period(
        &prep.values,
        options.model,
        options.seasonal_period,
        options.auto_detect_seasonality,
        !options.seasonal_periods.is_empty(),
    )?;

    let ts = make_timeseries(&prep.values)?;

    // Build → fit → introspect, per model. `.explanation()` returns
    // the typed enum; we serialise to JSON so the wire is one string.
    let explanation = match options.model {
        ModelType::AutoETS => {
            // D-07: an invalid model_pool must propagate as an error, never
            // be silently swallowed by `if let Ok(..)`.
            let pool = match options.model_pool.as_deref() {
                Some(p) => Some(parse_model_pool(p)?),
                None => None,
            };
            let mut model = new_auto_ets(period, pool);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("AutoETS fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::AutoARIMA => {
            let mut model = new_auto_arima(period);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("AutoARIMA fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::AutoTheta => {
            let mut model = new_auto_theta(period);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("AutoTheta fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::AutoTBATS => {
            // resolve_period guarantees period > 1 here whenever
            // seasonal_periods is empty (AutoTBATS requires a period, D-07).
            let periods: Vec<usize> = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            let mut model = AutoTBATS::new(periods);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("AutoTBATS fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::MFLES | ModelType::AutoMFLES => {
            // MFLES/AutoMFLES are not in requires_seasonal_period — a
            // non-seasonal vec![] periods list is a legitimate default.
            let periods: Vec<usize> = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else if period > 1 {
                vec![period]
            } else {
                vec![]
            };
            let mut model = MFLES::new(periods);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("MFLES fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::MSTL | ModelType::AutoMSTL => {
            // resolve_period guarantees period > 1 here whenever
            // seasonal_periods is empty (MSTL/AutoMSTL require a period, D-07).
            let periods: Vec<usize> = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            let mut model = MSTLForecaster::new(periods);
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("MSTL fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        ModelType::Laplace => {
            let variant = options.laplace_variant.unwrap_or_default();
            let mut model = match variant {
                LaplaceVariant::Auto => LaplaceForecaster::new().auto(),
                LaplaceVariant::AutoAid => LaplaceForecaster::new().auto_aid(),
                LaplaceVariant::Skaters => LaplaceForecaster::new().skaters(),
            };
            if period > 1 {
                model = model.with_seasonal(period);
            }
            if options.laplace_seasonal_batch_init && period > 1 {
                model = model.with_seasonal_batch_init();
            }
            model.fit(&ts).map_err(|e| {
                ForecastError::ComputationError(format!("Laplace fit failed: {e}"))
            })?;
            Inspectable::explanation(&model)
        }
        other => {
            return Err(ForecastError::InvalidModel(format!(
                "Model '{}' does not implement Inspectable. Supported models: \
                 AutoETS, AutoARIMA, AutoTheta, AutoTBATS, MFLES, AutoMFLES, MSTL, AutoMSTL, Laplace.",
                other.name()
            )));
        }
    }
    .map_err(|e| ForecastError::ComputationError(format!("explanation() failed: {e}")))?;

    serde_json::to_string(&explanation).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to serialise Explanation: {e}"))
    })
}

/// Per-horizon forecast decomposition via the crate's `Explainable` trait.
///
/// Fits the model, calls `.explain(horizon)`, and returns the
/// `ForecastExplanation` (level / trend / seasonal / residual +
/// named_components) as a JSON string.
///
/// Models covered (Explainable in anofox-forecast 0.15.3):
/// - ETS (fixed-spec)
/// - MSTLForecaster
/// - Theta (single-spec, not AutoTheta)
///
/// Unsupported models return an `InvalidModel` error naming the model.
pub fn forecast_explain(
    values: &[Option<f64>],
    horizon: usize,
    options: &ForecastOptions,
) -> Result<String> {
    use anofox_forecast::models::explain::Explainable;
    use anofox_forecast::models::exponential::{ETSSpec, ETS as ETSModel};
    use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
    use anofox_forecast::models::theta::Theta;
    use anofox_forecast::prelude::Forecaster;

    // Explicit input policy (D-11/D-12) — see forecast()'s matching comment.
    let prep = prepare_series(values)?;

    // D-07: resolve_period errors for MSTL/AutoMSTL with nothing detected
    // and no seasonal_periods list — never a silent hardcoded-period default.
    let period = resolve_period(
        &prep.values,
        options.model,
        options.seasonal_period,
        options.auto_detect_seasonality,
        !options.seasonal_periods.is_empty(),
    )?;

    let ts = make_timeseries(&prep.values)?;

    let explanation = match options.model {
        ModelType::ETS => {
            let spec = options.ets_spec.as_deref().unwrap_or("AAA");
            let parsed = ETSSpec::from_notation(spec).map_err(|e| {
                ForecastError::ComputationError(format!("Invalid ETS spec '{spec}': {e}"))
            })?;
            let mut model = ETSModel::new(parsed, period);
            model
                .fit(&ts)
                .map_err(|e| ForecastError::ComputationError(format!("ETS fit failed: {e}")))?;
            model.explain(horizon)
        }
        ModelType::MSTL | ModelType::AutoMSTL => {
            // resolve_period guarantees period > 1 here whenever
            // seasonal_periods is empty (MSTL/AutoMSTL require a period, D-07).
            let periods: Vec<usize> = if !options.seasonal_periods.is_empty() {
                options.seasonal_periods.clone()
            } else {
                vec![period]
            };
            let mut model = MSTLForecaster::new(periods);
            model
                .fit(&ts)
                .map_err(|e| ForecastError::ComputationError(format!("MSTL fit failed: {e}")))?;
            model.explain(horizon)
        }
        ModelType::Theta => {
            let mut model = if period > 1 {
                Theta::seasonal(period)
            } else {
                Theta::new()
            };
            model
                .fit(&ts)
                .map_err(|e| ForecastError::ComputationError(format!("Theta fit failed: {e}")))?;
            model.explain(horizon)
        }
        other => {
            return Err(ForecastError::InvalidModel(format!(
                "Model '{}' does not implement Explainable. Supported models: \
                 ETS, MSTL, AutoMSTL, Theta.",
                other.name()
            )));
        }
    }
    .map_err(|e| ForecastError::ComputationError(format!("explain() failed: {e}")))?;

    // ForecastExplanation isn't Serialize; hand-build the JSON.
    let mut obj = serde_json::Map::new();
    obj.insert(
        "level".to_string(),
        serde_json::to_value(&explanation.level).unwrap_or(serde_json::Value::Null),
    );
    if let Some(trend) = &explanation.trend {
        obj.insert(
            "trend".to_string(),
            serde_json::to_value(trend).unwrap_or(serde_json::Value::Null),
        );
    }
    if let Some(seasonal) = &explanation.seasonal {
        obj.insert(
            "seasonal".to_string(),
            serde_json::to_value(seasonal).unwrap_or(serde_json::Value::Null),
        );
    }
    if let Some(residual) = &explanation.residual {
        obj.insert(
            "residual".to_string(),
            serde_json::to_value(residual).unwrap_or(serde_json::Value::Null),
        );
    }
    let named: serde_json::Value = explanation
        .named_components
        .iter()
        .map(|(k, v)| {
            (
                k.clone(),
                serde_json::to_value(v).unwrap_or(serde_json::Value::Null),
            )
        })
        .collect::<serde_json::Map<_, _>>()
        .into();
    obj.insert("named_components".to_string(), named);
    serde_json::to_string(&serde_json::Value::Object(obj)).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to serialise ForecastExplanation: {e}"))
    })
}

/// AutoTheta: Automatic selection of best Theta variant (STM, OTM, DSTM, DOTM).
/// Uses the proper AutoTheta implementation from anofox-forecast library.
fn forecast_auto_theta(
    values: &[f64],
    horizon: usize,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    use chrono::{Duration, TimeZone, Utc};

    // Create timestamps for TimeSeries (required by anofox-forecast)
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();

    let ts = TimeSeries::univariate(timestamps, values.to_vec()).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to create TimeSeries: {}", e))
    })?;

    let mut model = new_auto_theta(period);

    // Fit the model
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("AutoTheta fit failed: {}", e)))?;

    // Get forecasts (D-01: native intervals)
    let forecast = model
        .predict_with_intervals(horizon, level)
        .map_err(|e| ForecastError::ComputationError(format!("AutoTheta predict failed: {}", e)))?;

    // Get the selected model type for model name
    let model_name = if let Some(selected) = model.selected_model() {
        format!("AutoTheta({})", selected)
    } else {
        "AutoTheta".to_string()
    };

    // Extract point forecasts (primary dimension)
    let point = forecast.primary().to_vec();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        model_name,
        aic: None,
        bic: None,
        mse: None,
    })
}

/// AutoTBATS: Automatic TBATS configuration selection.
/// Uses the proper AutoTBATS implementation from anofox-forecast library.
fn forecast_auto_tbats(
    values: &[f64],
    horizon: usize,
    periods: &[usize],
    level: f64,
) -> Result<ForecastOutput> {
    use chrono::{Duration, TimeZone, Utc};

    // Create timestamps for TimeSeries (required by anofox-forecast)
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();

    let ts = TimeSeries::univariate(timestamps, values.to_vec()).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to create TimeSeries: {}", e))
    })?;

    // Configure AutoTBATS with seasonal periods
    let mut model = AutoTBATS::new(periods.to_vec());

    // Fit the model
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("AutoTBATS fit failed: {}", e)))?;

    // Get forecasts (D-01: native intervals)
    let forecast = model
        .predict_with_intervals(horizon, level)
        .map_err(|e| ForecastError::ComputationError(format!("AutoTBATS predict failed: {}", e)))?;

    // Get the selected configuration for model name
    let model_name = if let Some(config) = model.selected_config() {
        format!("AutoTBATS({})", config)
    } else {
        "AutoTBATS".to_string()
    };

    // Extract point forecasts (primary dimension)
    let point = forecast.primary().to_vec();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        model_name,
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_mfles(
    values: &[f64],
    horizon: usize,
    periods: &[usize],
    level: f64,
) -> Result<ForecastOutput> {
    use chrono::{Duration, TimeZone, Utc};

    // Create timestamps for TimeSeries (required by anofox-forecast)
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();

    let ts = TimeSeries::univariate(timestamps, values.to_vec()).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to create TimeSeries: {}", e))
    })?;

    let mut model = MFLES::new(periods.to_vec());

    // Fit the model
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("MFLES fit failed: {}", e)))?;

    // Get forecasts (D-01: native intervals)
    let forecast = model
        .predict_with_intervals(horizon, level)
        .map_err(|e| ForecastError::ComputationError(format!("MFLES predict failed: {}", e)))?;

    // Extract point forecasts
    let point = forecast.primary().to_vec();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        // Empty model_name: the caller uses enum name (MFLES or AutoMFLES)
        model_name: String::new(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_mstl_lib(
    values: &[f64],
    horizon: usize,
    periods: &[usize],
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = MSTLForecaster::new(periods.to_vec());
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("MSTL fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "MSTL")
}

fn forecast_tbats_lib(
    values: &[f64],
    horizon: usize,
    periods: &[usize],
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = TBATSModel::new(periods.to_vec());
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("TBATS fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "TBATS")
}

/// Helper to create a TimeSeries from values (hourly timestamps).
fn make_timeseries(values: &[f64]) -> Result<TimeSeries> {
    use chrono::{Duration, TimeZone, Utc};
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec())
        .map_err(|e| ForecastError::ComputationError(format!("Failed to create TimeSeries: {}", e)))
}

/// A series that has been policy-processed and is ready for model fitting
/// (D-11/D-12, plan 10-01 Task 3).
///
/// - A valid `NaN` (`Some(f64::NAN)`) is treated exactly like `None` (missing).
/// - Leading/trailing missing values are trimmed; interior gaps are filled by
///   `fill_nulls_interpolate` on the TRIMMED slice (imputation.rs is untouched).
/// - `values`/`interpolated` are indexed relative to the trimmed slice (index 0
///   is the first originally-observed value); `lead_trim`/`trail_trim`/
///   `original_len` carry the information needed to re-align back onto the
///   caller's original array (see `finalize_output`).
#[derive(Debug, Clone)]
pub struct PreparedSeries {
    /// Trimmed, interior-interpolated values ready to hand to a crate model.
    pub values: Vec<f64>,
    /// `interpolated[i]` is `true` iff `values[i]` was filled in by
    /// interpolation rather than an originally-observed value.
    pub interpolated: Vec<bool>,
    /// Count of leading missing values dropped before `values[0]`.
    pub lead_trim: usize,
    /// Count of trailing missing values dropped after `values[last]`.
    pub trail_trim: usize,
    /// Length of the original (pre-trim) input array.
    pub original_len: usize,
}

/// Apply the explicit input policy (D-11) to a raw `Option<f64>` series:
/// `Some(NaN)` ≡ `None`; `±Inf` is a hard `InvalidInput` error (not missing);
/// an all-missing series (or one with fewer than 3 points after trimming)
/// is `InsufficientData`; leading/trailing missing values are trimmed;
/// interior gaps are interpolated via `fill_nulls_interpolate` on the
/// trimmed slice.
pub fn prepare_series(values: &[Option<f64>]) -> Result<PreparedSeries> {
    let original_len = values.len();

    // ±Inf is always a hard input error — name the first offending index.
    // (A valid NaN is explicitly NOT in this category: it is missing data,
    // not an invalid one, per D-11.)
    if let Some(idx) = values
        .iter()
        .position(|v| matches!(v, Some(x) if x.is_infinite()))
    {
        return Err(ForecastError::InvalidInput(format!(
            "value at index {idx} is ±Infinity, which is not a valid forecast input \
             (use NULL for missing data)"
        )));
    }

    // Some(NaN) ≡ None (D-11): normalize before locating the trim bounds.
    let normalized: Vec<Option<f64>> = values.iter().map(|v| v.filter(|x| !x.is_nan())).collect();

    let first_idx = normalized.iter().position(|v| v.is_some());
    let last_idx = normalized.iter().rposition(|v| v.is_some());

    let (first_idx, last_idx) = match (first_idx, last_idx) {
        (Some(f), Some(l)) => (f, l),
        _ => return Err(ForecastError::InsufficientData { needed: 3, got: 0 }),
    };

    let lead_trim = first_idx;
    let trail_trim = original_len - 1 - last_idx;
    let trimmed = &normalized[first_idx..=last_idx];

    if trimmed.len() < 3 {
        return Err(ForecastError::InsufficientData {
            needed: 3,
            got: trimmed.len(),
        });
    }

    // trimmed[0] and trimmed[last] are both Some by construction (first_idx/
    // last_idx are the first/last Some positions), so fill_nulls_interpolate
    // only fills INTERIOR gaps here — no edge flat-extension applies.
    let interpolated_mask: Vec<bool> = trimmed.iter().map(|v| v.is_none()).collect();
    let interpolated_values = fill_nulls_interpolate(trimmed);

    Ok(PreparedSeries {
        values: interpolated_values,
        interpolated: interpolated_mask,
        lead_trim,
        trail_trim,
        original_len,
    })
}

/// Re-align a raw model output (fitted on `prep.values` for
/// `horizon + prep.trail_trim` steps) onto the caller's original indexing
/// (D-12): drop the leading `trail_trim` forecast steps so step 1 is the
/// period after the ORIGINAL last row (the date anchor the C++ layer uses),
/// mark interpolated-position residuals as NaN (they measure interpolation,
/// not the model), compute MSE over the remaining finite residuals, and pad
/// fitted/residuals back out to `original_len` with NaN at the
/// leading/trailing trimmed positions.
///
/// Crate fitted/residuals arrays whose length doesn't match
/// `prep.values.len()` are treated as unavailable (T-10-02: never index a
/// crate array of unexpected length) rather than causing a panic or being
/// silently misaligned.
pub fn finalize_output(
    raw: ForecastOutput,
    prep: &PreparedSeries,
    horizon: usize,
    include_fitted: bool,
    include_residuals: bool,
) -> ForecastOutput {
    let trail_trim = prep.trail_trim;

    // raw.point/lower/upper were generated for `horizon + trail_trim` steps;
    // drop the first `trail_trim` so step 1 corresponds to the period after
    // the ORIGINAL last row, then cap at `horizon` entries.
    let drop_and_align = |v: Vec<f64>| -> Vec<f64> {
        if v.len() > trail_trim {
            v[trail_trim..].iter().take(horizon).copied().collect()
        } else {
            vec![]
        }
    };

    let point = drop_and_align(raw.point);
    let mut lower = drop_and_align(raw.lower);
    let mut upper = drop_and_align(raw.upper);
    if lower.len() != point.len() {
        lower = vec![];
    }
    if upper.len() != point.len() {
        upper = vec![];
    }

    let fitted_vals = raw.fitted.filter(|f| f.len() == prep.values.len());

    // MSE = mean of r^2 over finite residuals — always computed when the
    // crate exposes usable residuals, regardless of include_residuals (which
    // only gates whether the array itself is returned to the caller). This is
    // computed from `raw.residuals` directly (not the length-filtered,
    // position-aligned `residuals_vals` below): a scalar mean doesn't need
    // positional alignment the way a returned array does, so a model whose
    // residuals length differs from `prep.values.len()` (e.g. AutoARIMA,
    // whose crate residuals drop the d+D*s differencing warmup rows instead
    // of NaN-padding them) still gets a real MSE — just without the
    // interpolated-position exclusion below, since position-correspondence
    // with `prep.interpolated` is unknown for a shorter/longer array.
    let mse = raw.residuals.as_ref().and_then(|r| {
        let finite: Vec<f64> = if r.len() == prep.values.len() {
            r.iter()
                .zip(prep.interpolated.iter())
                .filter(|(v, is_interp)| v.is_finite() && !**is_interp)
                .map(|(v, _)| *v)
                .collect()
        } else {
            r.iter().copied().filter(|v| v.is_finite()).collect()
        };
        if finite.is_empty() {
            None
        } else {
            Some(finite.iter().map(|v| v.powi(2)).sum::<f64>() / finite.len() as f64)
        }
    });

    // For the EXPORTED residuals array (T-10-02): only a length-matched array
    // can be safely padded/position-aligned back onto the original indexing;
    // a mismatched length is treated as unavailable here, independent of the
    // MSE fallback above (which only needs a scalar, not positional alignment).
    let mut residuals_vals = raw.residuals.filter(|r| r.len() == prep.values.len());

    // Residuals at interpolated positions measure the interpolation, not the
    // model — exclude them from the EXPORTED array by marking NaN (D-12).
    if let Some(ref mut r) = residuals_vals {
        for (i, is_interp) in prep.interpolated.iter().enumerate() {
            if *is_interp {
                r[i] = f64::NAN;
            }
        }
    }

    // Pad fitted/residuals back out to the ORIGINAL length, with NaN at the
    // leading/trailing positions that were trimmed before fitting.
    let pad_to_original = |v: Vec<f64>| -> Vec<f64> {
        let mut out = vec![f64::NAN; prep.original_len];
        for (i, val) in v.into_iter().enumerate() {
            out[prep.lead_trim + i] = val;
        }
        out
    };

    let fitted = if include_fitted {
        fitted_vals.map(pad_to_original)
    } else {
        None
    };
    let residuals = if include_residuals {
        residuals_vals.map(pad_to_original)
    } else {
        None
    };

    ForecastOutput {
        point,
        lower,
        upper,
        fitted,
        residuals,
        model_name: raw.model_name,
        aic: raw.aic,
        bic: raw.bic,
        mse,
    }
}

/// Slice/align an `ExogenousData` onto a `PreparedSeries`'s trimmed range
/// (D-12 exog variant of the same trailing-NULL alignment policy as
/// `finalize_output`): each historical regressor is sliced to
/// `[lead_trim, original_len - trail_trim)`, and the trimmed trailing rows'
/// own historical values are prepended onto each future regressor so the
/// aligned future length is `horizon + trail_trim` — matching the extended
/// horizon the model is actually asked to predict.
fn align_exog(prep: &PreparedSeries, exog: &ExogenousData) -> ExogenousData {
    let start = prep.lead_trim;
    let end = prep.original_len.saturating_sub(prep.trail_trim);

    let historical: Vec<Vec<f64>> = exog
        .historical
        .iter()
        .map(|col| {
            let end = end.min(col.len());
            let start = start.min(end);
            col[start..end].to_vec()
        })
        .collect();

    let future: Vec<Vec<f64>> = exog
        .future
        .iter()
        .zip(exog.historical.iter())
        .map(|(fcol, hcol)| {
            let trail_start = end.min(hcol.len());
            let mut out = hcol[trail_start..].to_vec();
            out.extend_from_slice(fcol);
            out
        })
        .collect();

    ExogenousData { historical, future }
}

/// Extract `(lower, upper)` from a crate `Forecast`'s first (univariate) interval
/// dimension. A crate model with no native interval support (`lower()`/`upper()`
/// return `None`), or whose returned bound length does not match `point_len`,
/// yields empty vecs — NULL lower/upper at the FFI/SQL layer (D-02) — never a
/// partial, misaligned, or fabricated array. This is the single bounds-extraction
/// path used by every forecast_* function in this module (no z-value or interval
/// arithmetic may be computed here or anywhere else in this file — FCST-02).
fn extract_bounds(
    forecast: &anofox_forecast::core::Forecast,
    point_len: usize,
) -> (Vec<f64>, Vec<f64>) {
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
    let lower = if lower.len() == point_len {
        lower
    } else {
        vec![]
    };
    let upper = if upper.len() == point_len {
        upper
    } else {
        vec![]
    };
    (lower, upper)
}

/// Helper to extract ForecastOutput from a fitted Forecaster model.
///
/// Uses `predict_with_intervals(horizon, level)` (not `predict`) so `lower`/`upper`
/// reflect the model's own interval theory (D-01). A crate `Forecast` with no native
/// interval support returns `lower()`/`upper()` as `None`, which this helper surfaces
/// as empty vecs (D-02) — never a fabricated width.
///
/// `name_override`: use this instead of model.name() to match our ModelType enum names.
fn extract_forecast(
    model: &dyn Forecaster,
    horizon: usize,
    level: f64,
    name_override: &str,
) -> Result<ForecastOutput> {
    let forecast = model.predict_with_intervals(horizon, level).map_err(|e| {
        ForecastError::ComputationError(format!("{} predict failed: {}", name_override, e))
    })?;

    let point = forecast.primary().to_vec();
    let (lower, upper) = extract_bounds(&forecast, point.len());

    Ok(ForecastOutput {
        point,
        lower,
        upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        model_name: name_override.to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_croston_classic(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = Croston::new();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Croston fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "CrostonClassic")
}

fn forecast_croston_optimized(
    values: &[f64],
    horizon: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = Croston::new().optimized();
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("CrostonOptimized fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, level, "CrostonOptimized")
}

fn forecast_croston_sba(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = Croston::new().sba();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("CrostonSBA fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "CrostonSBA")
}

fn forecast_tsb(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = TSB::new();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("TSB fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "TSB")
}

fn forecast_adida(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = ADIDA::new();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("ADIDA fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "ADIDA")
}

fn forecast_imapa(values: &[f64], horizon: usize, level: f64) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = IMAPA::new();
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("IMAPA fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "IMAPA")
}

// ============================================================================
// Classical model implementations (Phase 3)
// ============================================================================

/// Forecast conditional volatility using GARCH(p, q).
///
/// Returns `sqrt(forecast_variance(horizon))` — the conditional standard
/// deviation (volatility), NOT the variance.  Do NOT use
/// `Forecaster::predict()` for this: it returns seed-1 simulated innovations,
/// not the analytical variance forecast.
///
/// `lower`/`upper` are a passthrough of `predict_with_intervals(horizon, level)`
/// (D-01): those bounds are centred on the crate's simulated-innovation path,
/// not on `point ± z*sqrt(variance)` — intentional, not a bug (see plan 10-01 D-01).
/// `fitted`/`residuals` come from the crate's own `fitted_values()`/`residuals()`.
///
/// Requires `p + q + 10` observations minimum (GARCH(1,1) → 12 min).
fn forecast_garch(
    values: &[f64],
    horizon: usize,
    p: usize,
    q: usize,
    level: f64,
) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = GARCH::new(p, q);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH fit failed: {}", e)))?;
    // IMPORTANT: use forecast_variance(), NOT predict() — predict() returns simulated innovations
    let variance = model
        .forecast_variance(horizon)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH forecast failed: {}", e)))?;
    // Output is volatility (std-dev = sqrt of variance), not variance itself
    let volatility: Vec<f64> = variance.iter().map(|&v| v.sqrt()).collect();

    let forecast = model
        .predict_with_intervals(horizon, level)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH predict failed: {}", e)))?;
    let (lower, upper) = extract_bounds(&forecast, volatility.len());
    let fitted = model.fitted_values().map(|v| v.to_vec());
    let residuals = model.residuals().map(|v| v.to_vec());

    Ok(ForecastOutput {
        point: volatility,
        lower,
        upper,
        fitted,
        residuals,
        model_name: format!("GARCH({},{})", p, q),
        aic: None,
        bic: None,
        mse: None,
    })
}

/// Forecast using a Kalman filter state-space model (D-07).
///
/// `spec`: `None`/`""`/`"local_level"` (case-insensitive) → local level
/// (documented default); `"local_linear_trend"` → local linear trend;
/// anything else is `InvalidParameter { param: "kalman_model" }` — parsed
/// via [`parse_kalman_spec`], never a silent default. model_name is always
/// `"Kalman(local_level)"` or `"Kalman(local_linear_trend)"`, naming the
/// spec that actually ran.
///
/// Uses `extract_forecast` because `KalmanForecaster` implements the
/// `Forecaster` trait — same path as all other trait-based models.
fn forecast_kalman(
    values: &[f64],
    horizon: usize,
    spec: Option<&str>,
    level: f64,
) -> Result<ForecastOutput> {
    let kalman_spec = parse_kalman_spec(spec)?;
    let ts = make_timeseries(values)?;
    let mut model = new_kalman(kalman_spec);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
    // KalmanForecaster implements Forecaster — extract_forecast works directly
    let name = format!("Kalman({})", kalman_spec.tag());
    extract_forecast(&model, horizon, level, &name)
}

/// Parse a `combination_method` string to a `CombinationMethod` enum variant.
///
/// Empty string and `"mean"` both map to `CombinationMethod::Mean` (Phase 4 default),
/// overriding the crate's `WeightedMSE` default. `"custom"` is explicitly rejected
/// (deferred, ENS-F1). All other unknown strings return `InvalidParameter`.
///
/// `"stacking"` maps to `CombinationMethod::Stacking { folds: 2 }` — a second-half
/// in-sample holdout used to fit ridge-stacking weights. The fold count is fixed and
/// not user-configurable in v1.
fn parse_combination_method(
    s: Option<&str>,
) -> Result<anofox_forecast::models::ensemble::CombinationMethod> {
    use anofox_forecast::models::ensemble::CombinationMethod;
    match s.unwrap_or("").trim().to_lowercase().as_str() {
        "" | "mean" => Ok(CombinationMethod::Mean),
        "median" => Ok(CombinationMethod::Median),
        "weighted_mse" | "weightedmse" | "weighted-mse" => Ok(CombinationMethod::WeightedMSE),
        "inverse_aic" | "inverseaic" | "inverse-aic" | "aic" => Ok(CombinationMethod::InverseAIC),
        "stacking" | "stack" => Ok(CombinationMethod::Stacking { folds: 2 }),
        "horizon_adaptive" | "horizonadaptive" | "horizon-adaptive" | "adaptive" => {
            Ok(CombinationMethod::HorizonAdaptive)
        }
        other => Err(ForecastError::InvalidParameter {
            param: "combination_method".to_string(),
            value: other.to_string(),
            reason: "expected one of: mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive"
                .to_string(),
        }),
    }
}

/// Auto-ensemble forecasting helper.
///
/// Fits AutoARIMA, AutoETS, and AutoTheta; ranks candidates by in-sample MSE
/// ascending; combines the top-`top_k` members using the specified `CombinationMethod`.
///
/// Mirrors `forecast_kalman` in structure — uses `extract_forecast` via the
/// `Forecaster` trait, so lower/upper/fitted/residuals are a passthrough of the
/// crate `AutoEnsemble`'s own `predict_with_intervals`/`fitted_values`/`residuals`
/// (D-01); empty/NULL only when the crate itself returns no bounds (D-02).
fn forecast_auto_ensemble(
    values: &[f64],
    horizon: usize,
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
    level: f64,
) -> Result<ForecastOutput> {
    use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};

    let combination_method = parse_combination_method(method_str)?;
    let config = AutoEnsembleConfig {
        top_k,
        combination_method,
        seasonal_period: if period > 1 { Some(period) } else { None },
    };
    let ts = make_timeseries(values)?;
    let mut model = AutoEnsemble::with_config(config);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e)))?;
    extract_forecast(&model, horizon, level, "AutoEnsemble")
}

// ============================================================================
// Phase 5 (ENS-02): Explicit-member ensemble helpers
// ============================================================================

/// Construct a boxed `Forecaster` instance for the given model type and optional
/// seasonal period.  Used by `forecast_explicit_ensemble` to build member models
/// for `Ensemble::new(members)`.
///
/// Compiler-exhaustive match over all 36 `ModelType` variants — any future variant
/// addition causes a compile error rather than a silent runtime miss.
///
/// Returns `Err(InvalidParameter)` for model types that are not expressible as a
/// single per-series `Box<dyn Forecaster>` with a shared `seasonal_period`:
/// GARCH (wrong predict semantics), Laplace (variant-dependent construction),
/// ARIMA (requires p/d/q), MFLES/MSTL/TBATS + Auto variants (multi-seasonal),
/// AutoEnsemble (circular).
pub(crate) fn build_forecaster(
    model_type: ModelType,
    period: Option<usize>,
    n_obs: usize,
) -> Result<anofox_forecast::models::BoxedForecaster> {
    use anofox_forecast::models::baseline::{Naive, RandomWalkWithDrift};

    let p = period.unwrap_or(0);

    // D-07: a seasonal-required member reaching here with no usable period
    // is a hard error naming the member and `seasonal_period` — never a
    // silent default of 12. `forecast_explicit_ensemble`/
    // `inspect_explicit_ensemble` resolve a shared period before calling
    // this function, so this is a defensive guard for direct callers.
    let require_period = |p: usize| -> Result<usize> {
        if p > 1 {
            Ok(p)
        } else {
            Err(ForecastError::InvalidParameter {
                param: "seasonal_period".to_string(),
                value: model_type.name().to_string(),
                reason: format!(
                    "{} requires a seasonal period > 1 for every ensemble member; pass \
                     seasonal_period explicitly",
                    model_type.name()
                ),
            })
        }
    };

    match model_type {
        // Auto-selection models
        ModelType::AutoARIMA => Ok(Box::new(new_auto_arima(p))),
        ModelType::AutoETS => Ok(Box::new(new_auto_ets(p, None))),
        ModelType::AutoTheta => Ok(Box::new(new_auto_theta(p))),
        // Theta family (not in requires_seasonal_period — non-seasonal is a
        // legitimate, documented default, same as the standalone path)
        ModelType::Theta => Ok(Box::new(new_theta(p))),
        ModelType::OptimizedTheta => Ok(Box::new(new_optimized_theta(p))),
        ModelType::DynamicTheta => Ok(Box::new(new_dynamic_theta(p))),
        ModelType::DynamicOptimizedTheta => Ok(Box::new(new_dynamic_optimized_theta(p))),
        // Baselines (crate types that implement Forecaster)
        ModelType::Naive => Ok(Box::new(Naive::new())),
        ModelType::RandomWalkDrift => Ok(Box::new(RandomWalkWithDrift::new())),
        ModelType::SES => Ok(Box::new(SimpleExponentialSmoothing::new(0.3))),
        ModelType::SESOptimized => Ok(Box::new(SimpleExponentialSmoothing::auto())),
        ModelType::SMA => Ok(Box::new(new_sma(p.max(3)))),
        ModelType::Holt => Ok(Box::new(HoltLinearTrend::auto())),
        ModelType::HoltWinters => Ok(Box::new(new_holt_winters(require_period(p)?))),
        ModelType::SeasonalNaive => Ok(Box::new(new_seasonal_naive(require_period(p)?))),
        ModelType::SeasonalES => Ok(Box::new(new_seasonal_es(require_period(p)?))),
        ModelType::SeasonalESOptimized => {
            Ok(Box::new(new_seasonal_es_optimized(require_period(p)?)))
        }
        ModelType::SeasonalWindowAverage => {
            let sp = require_period(p)?;
            Ok(Box::new(new_seasonal_window_average(sp, n_obs)))
        }
        // 'ETS' without a spec means the same crate AutoETS selection as
        // standalone 'AutoETS' (D-05/FCST-07) — an ensemble member built
        // from 'ETS' must match the standalone 'ETS' result bit-for-bit.
        ModelType::ETS => Ok(Box::new(new_auto_ets(p, None))),
        // State-space
        ModelType::Kalman => Ok(Box::new(new_kalman(KalmanSpec::LocalLevel))),
        // Intermittent demand
        ModelType::CrostonClassic => Ok(Box::new(Croston::new())),
        ModelType::CrostonOptimized => Ok(Box::new(Croston::new().optimized())),
        ModelType::CrostonSBA => Ok(Box::new(Croston::new().sba())),
        ModelType::ADIDA => Ok(Box::new(ADIDA::new())),
        ModelType::IMAPA => Ok(Box::new(IMAPA::new())),
        ModelType::TSB => Ok(Box::new(TSB::new())),

        // NOT SUPPORTED — return error naming the model and suggesting an alternative
        ModelType::GARCH => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "GARCH is not supported as an ensemble member: Forecaster::predict() \
                     returns simulated innovations, not level forecasts; use AutoARIMA \
                     or AutoETS instead"
                .to_string(),
        }),
        ModelType::Laplace => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "Laplace is not supported as an ensemble member in v1 (variant-dependent \
                     construction); use AutoARIMA, AutoETS, or AutoTheta instead"
                .to_string(),
        }),
        ModelType::ARIMA => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "Fixed-order ARIMA requires (p,d,q) params not supported in the \
                     shared-period ensemble v1; use AutoARIMA instead"
                .to_string(),
        }),
        ModelType::MFLES => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "MFLES requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::AutoMFLES => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "AutoMFLES requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::MSTL => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "MSTL requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::AutoMSTL => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "AutoMSTL requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::TBATS => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "TBATS requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::AutoTBATS => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "AutoTBATS requires seasonal_periods[] which is not supported in v1 \
                     explicit-member ensembles; use AutoARIMA or AutoETS instead"
                .to_string(),
        }),
        ModelType::AutoEnsemble => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "AutoEnsemble cannot be used as a member of an explicit ensemble".to_string(),
        }),
    }
}

/// Explicit-member ensemble forecasting.
///
/// Fits each named member model independently on the same series and combines
/// their point forecasts using the specified `CombinationMethod` (default: Mean).
///
/// Mirrors `forecast_auto_ensemble` but accepts an explicit list of member names
/// instead of running automated model selection.
///
/// # Arguments
/// * `values` — series values (length >= 2)
/// * `horizon` — number of steps to forecast
/// * `member_names` — ordered list of member model names (must have >= 2 items);
///   names follow the same vocabulary as `ts_forecast_by` (ModelType::from_str)
/// * `method_str` — combination method string; `None` or `""` → Mean
/// * `period` — shared seasonal period (0 or 1 → non-seasonal)
///
/// Returns `ForecastOutput` with `model_name = "Ensemble"` and `lower = upper = []`
/// (prediction intervals are Phase 6, EPI-01).
pub fn forecast_explicit_ensemble(
    values: &[Option<f64>],
    horizon: usize,
    member_names: &[String],
    method_str: Option<&str>,
    period: usize,
    confidence_level: f64,
) -> Result<ForecastOutput> {
    use anofox_forecast::models::ensemble::Ensemble;

    // 1. Validate member count
    if member_names.len() < 2 {
        return Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: member_names.len().to_string(),
            reason: "at least 2 members are required for an ensemble".to_string(),
        });
    }

    // 1b. Validate confidence_level (D-01, plan 10-03 Task 3)
    if !(confidence_level > 0.0 && confidence_level < 1.0) {
        return Err(ForecastError::InvalidParameter {
            param: "confidence_level".to_string(),
            value: confidence_level.to_string(),
            reason: "must be in the open interval (0, 1)".to_string(),
        });
    }

    // 2. Parse combination method (reuse Phase 4 function verbatim — one definition)
    let combination_method = parse_combination_method(method_str)?;

    // 3. Explicit input policy (D-11/D-12) — mirrors the main forecast() path,
    // including the trailing-NULL-aware extended horizon below.
    let prep = prepare_series(values)?;

    // 4. Build member forecasters. `period == 0` auto-detects a shared
    // period (D-07 — matches `ts_forecast_by`'s own `seasonal_period=0`
    // default); an explicit `period > 1` always wins. A seasonal-required
    // member reaching `build_forecaster` with no usable period is a hard
    // error naming that member (build_forecaster's own guard).
    let detected_period = if period > 1 {
        period
    } else {
        detect_seasonality(&prep.values, None)
            .ok()
            .and_then(|p| p.first().copied())
            .map(|p| p as usize)
            .unwrap_or(0)
    };
    let member_period = if detected_period > 1 {
        Some(detected_period)
    } else {
        None
    };
    let mut members: Vec<anofox_forecast::models::BoxedForecaster> =
        Vec::with_capacity(member_names.len());
    for name in member_names {
        let model_type: ModelType = name.parse().map_err(|_| ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: name.clone(),
            reason: format!(
                "unknown model name '{}'; use the same names as ts_forecast_by",
                name
            ),
        })?;
        members.push(build_forecaster(
            model_type,
            member_period,
            prep.values.len(),
        )?);
    }

    // 5. Build + fit ensemble + extract forecast
    // Ensemble implements Forecaster (anofox-forecast 0.15.3, model.rs:575)
    let ts = make_timeseries(&prep.values)?;
    let mut ens = Ensemble::new(members).with_method(combination_method);
    ens.fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Ensemble fit failed: {}", e)))?;
    // model_name = "Ensemble" (plain, mirroring Phase 4's plain "AutoEnsemble").
    // D-01 (plan 10-03 Task 3): lower/upper are the crate Ensemble's own
    // predict_with_intervals bounds (pooled across members) at
    // confidence_level — no longer hardcoded empty. finalize_output is still
    // used for the shared trailing-NULL alignment policy (D-12), with
    // include_fitted/include_residuals forced false since the FFI ensemble
    // path (anofox_ts_forecast_ensemble) always nulls those fields
    // regardless of what this function returns.
    let extended_horizon = horizon + prep.trail_trim;
    let raw = extract_forecast(&ens, extended_horizon, confidence_level, "Ensemble")?;
    Ok(finalize_output(raw, &prep, horizon, false, false))
}

// ============================================================================
// Phase 6 (INSP-01): Ensemble introspection functions
// ============================================================================

/// Inspect combination weights for an explicit-member ensemble (INSP-01).
///
/// Builds the Ensemble from the named members, fits it on the series, then
/// returns the per-member weights. For Mean and Median, weights equal 1/k.
/// For WeightedMSE, InverseAIC, Stacking, and HorizonAdaptive, weights are
/// crate-computed and sum to 1.0. HorizonAdaptive weights are the AVERAGE
/// of per-horizon weights (use horizon_weights() for per-step detail; that is
/// out of scope for INSP-01 v1).
///
/// # Arguments
/// * `values` - Time series values (with Option<f64> for nulls)
/// * `member_names` - Names of the ensemble member models (at least 2)
/// * `method_str` - Combination method string (None/empty → "mean")
/// * `period` - Seasonal period (0 or 1 → no seasonality)
///
/// # Returns
/// `Vec<(member_name, weight)>` — k entries, one per member.
pub fn inspect_explicit_ensemble(
    values: &[Option<f64>],
    member_names: &[String],
    method_str: Option<&str>,
    period: usize,
) -> Result<Vec<(String, f64)>> {
    use anofox_forecast::models::ensemble::Ensemble;

    // 1. Validate member count (same guard as forecast_explicit_ensemble)
    if member_names.len() < 2 {
        return Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: member_names.len().to_string(),
            reason: "at least 2 members are required for an ensemble".to_string(),
        });
    }

    // 2. Parse combination method (reuse Phase 4 function verbatim)
    let combination_method = parse_combination_method(method_str)?;

    // 3. Explicit input policy (D-11/D-12) — mirrors forecast_explicit_ensemble.
    let prep = prepare_series(values)?;

    // 4. Build member forecasters (same loop + period-0-detect as
    // forecast_explicit_ensemble, D-07).
    let detected_period = if period > 1 {
        period
    } else {
        detect_seasonality(&prep.values, None)
            .ok()
            .and_then(|p| p.first().copied())
            .map(|p| p as usize)
            .unwrap_or(0)
    };
    let member_period = if detected_period > 1 {
        Some(detected_period)
    } else {
        None
    };
    let mut members: Vec<anofox_forecast::models::BoxedForecaster> =
        Vec::with_capacity(member_names.len());
    for name in member_names {
        let model_type: ModelType = name.parse().map_err(|_| ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: name.clone(),
            reason: format!(
                "unknown model name '{}'; use the same names as ts_forecast_by",
                name
            ),
        })?;
        members.push(build_forecaster(
            model_type,
            member_period,
            prep.values.len(),
        )?);
    }

    // 5. Build + fit ensemble — DIVERGE: call .weights() instead of extract_forecast
    // NOTE: weights() called AFTER .fit() — pre-fit weights are always uniform (RESEARCH Pitfall 2)
    let ts = make_timeseries(&prep.values)?;
    let mut ens = Ensemble::new(members).with_method(combination_method);
    ens.fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Ensemble fit failed: {}", e)))?;

    let weights = ens.weights();
    Ok(member_names
        .iter()
        .cloned()
        .zip(weights.iter().copied())
        .collect())
}

/// Inspect selected members and in-sample MSE scores for an AutoEnsemble (INSP-01).
///
/// Builds and fits an AutoEnsemble, then returns the selected top-K members
/// (the first model_count entries from all_scores()) with their MSE scores
/// and — for Mean combination only — equal combination weights.
///
/// For WeightedMSE, InverseAIC, Stacking, and HorizonAdaptive, the combination
/// weight is `None` (NULL in SQL) because the inner ensemble's weights are not
/// accessible via the crate 0.15.3 public API — this is an upstream limitation,
/// not a gap in the implementation. Do NOT re-implement selection to fabricate weights.
///
/// # Arguments
/// * `values` - Time series values as concrete f64 (NaN for nulls; AutoEnsemble path)
/// * `top_k` - Number of models to select (0 → crate default of 3)
/// * `method_str` - Combination method string (None/empty → "weighted_mse" crate default)
/// * `period` - Seasonal period (0 or 1 → no seasonality)
///
/// # Returns
/// `Vec<(member_name, mse_score, weight)>` — k entries:
///   - `weight` is `Some(1/k)` for Mean, `None` for all other methods.
pub fn inspect_auto_ensemble(
    values: &[f64],
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
) -> Result<Vec<(String, f64, Option<f64>)>> {
    use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig, CombinationMethod};

    let combination_method = parse_combination_method(method_str)?;
    let config = AutoEnsembleConfig {
        top_k,
        combination_method,
        seasonal_period: if period > 1 { Some(period) } else { None },
    };
    let ts = make_timeseries(values)?;
    let mut model = AutoEnsemble::with_config(config);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e)))?;

    // Read selected top-K: first model_count() entries of all_scores()
    // all_scores() returns ALL candidates sorted ascending by MSE; first k are selected
    let k = model.model_count();
    let selected: Vec<(String, f64)> = model.all_scores().iter().take(k).cloned().collect();

    // Weight: Some(1/k) for Mean only; None for all other methods (crate 0.15.3 limitation)
    let weight_if_mean: Option<f64> = match combination_method {
        CombinationMethod::Mean => {
            if k > 0 {
                Some(1.0 / k as f64)
            } else {
                None
            }
        }
        _ => None,
    };

    Ok(selected
        .into_iter()
        .map(|(name, score)| (name, score, weight_if_mean))
        .collect())
}

#[cfg(test)]
mod insp01_tests {
    use super::*;

    fn synthetic_series(n: usize) -> Vec<Option<f64>> {
        (0..n)
            .map(|i| Some(10.0 + i as f64 * 0.5 + (i as f64).sin()))
            .collect()
    }

    fn synthetic_series_f64(n: usize) -> Vec<f64> {
        (0..n)
            .map(|i| 10.0 + i as f64 * 0.5 + (i as f64).sin())
            .collect()
    }

    #[test]
    fn inspect_explicit_mean_weights_equal_1_over_k() {
        let values = synthetic_series(60);
        let members = vec![
            "AutoARIMA".to_string(),
            "AutoETS".to_string(),
            "Naive".to_string(),
        ];
        let result = inspect_explicit_ensemble(&values, &members, Some("mean"), 0).unwrap();
        assert_eq!(result.len(), 3, "Should have 3 member entries");
        let k = 3.0_f64;
        for (name, weight) in &result {
            assert!(
                (weight - 1.0 / k).abs() < 1e-10,
                "Mean weight for {} should be 1/k but got {}",
                name,
                weight
            );
        }
        let sum: f64 = result.iter().map(|(_, w)| w).sum();
        assert!(
            (sum - 1.0).abs() < 1e-10,
            "Sum of Mean weights should be 1.0, got {}",
            sum
        );
    }

    #[test]
    fn inspect_explicit_weighted_mse_weights_sum_to_1() {
        let values = synthetic_series(60);
        let members = vec![
            "AutoARIMA".to_string(),
            "AutoETS".to_string(),
            "Naive".to_string(),
        ];
        let result = inspect_explicit_ensemble(&values, &members, Some("weighted_mse"), 0).unwrap();
        assert_eq!(result.len(), 3);
        let sum: f64 = result.iter().map(|(_, w)| w).sum();
        assert!(
            (sum - 1.0).abs() < 1e-6,
            "WeightedMSE weights should sum to 1.0, got {}",
            sum
        );
        for (_, w) in &result {
            assert!(
                *w >= 0.0,
                "WeightedMSE weight should be non-negative, got {}",
                w
            );
        }
    }

    #[test]
    fn inspect_explicit_too_few_members_returns_error() {
        let values = synthetic_series(20);
        let members = vec!["AutoARIMA".to_string()];
        let result = inspect_explicit_ensemble(&values, &members, Some("mean"), 0);
        assert!(result.is_err(), "Should return error for < 2 members");
        let err = result.unwrap_err().to_string();
        assert!(
            err.contains("members"),
            "Error message should mention 'members', got: {}",
            err
        );
    }

    #[test]
    fn inspect_explicit_unknown_member_returns_error() {
        let values = synthetic_series(20);
        let members = vec!["AutoARIMA".to_string(), "UnknownModel".to_string()];
        let result = inspect_explicit_ensemble(&values, &members, Some("mean"), 0);
        assert!(
            result.is_err(),
            "Should return error for unknown member name"
        );
    }

    #[test]
    fn inspect_auto_mean_returns_weight_some_1_over_k() {
        let values = synthetic_series_f64(60);
        let result = inspect_auto_ensemble(&values, 3, Some("mean"), 0).unwrap();
        assert!(!result.is_empty(), "Should return at least one member");
        let k = result.len() as f64;
        for (name, score, weight) in &result {
            assert!(
                *score > 0.0,
                "MSE score for {} should be > 0, got {}",
                name,
                score
            );
            let w = weight.expect("Mean combination should return Some weight");
            assert!(
                (w - 1.0 / k).abs() < 1e-10,
                "Mean weight for {} should be 1/k but got {}",
                name,
                w
            );
        }
    }

    #[test]
    fn inspect_auto_weighted_mse_returns_weight_none() {
        let values = synthetic_series_f64(60);
        // empty method_str → "weighted_mse" (crate default for AutoEnsemble)
        let result = inspect_auto_ensemble(&values, 3, Some("weighted_mse"), 0).unwrap();
        assert!(!result.is_empty(), "Should return at least one member");
        for (name, score, weight) in &result {
            assert!(
                *score > 0.0,
                "MSE score for {} should be > 0, got {}",
                name,
                score
            );
            assert!(
                weight.is_none(),
                "WeightedMSE combination should return None weight for {}, got {:?}",
                name,
                weight
            );
        }
    }

    #[test]
    fn inspect_auto_takes_only_model_count_selected_members() {
        let values = synthetic_series_f64(60);
        let top_k = 2;
        let result = inspect_auto_ensemble(&values, top_k, Some("mean"), 0).unwrap();
        assert!(
            result.len() <= top_k,
            "Should not return more than top_k={} members, got {}",
            top_k,
            result.len()
        );
    }
}

// ============================================================================
// Exogenous-aware forecasting functions
// ============================================================================

use anofox_regression::prelude::*;

/// Fit OLS regression: y = X * beta using anofox-regression
/// Returns coefficients (intercept + betas) and residuals
fn fit_ols_regression(y: &[f64], x: &[Vec<f64>]) -> (Vec<f64>, Vec<f64>) {
    let n = y.len();
    let k = x.len(); // number of regressors

    if k == 0 || n == 0 {
        return (vec![], y.to_vec());
    }

    // Build design matrix using faer: n_obs rows × k columns
    let x_mat = faer::Mat::from_fn(n, k, |i, j| x[j][i]);
    let y_col = faer::Col::from_fn(n, |i| y[i]);

    // Fit OLS with intercept
    let fitted = match OlsRegressor::builder()
        .with_intercept(true)
        .build()
        .fit(&x_mat, &y_col)
    {
        Ok(f) => f,
        Err(_) => {
            // Fallback: return zeros and original y as residuals
            return (vec![0.0; k + 1], y.to_vec());
        }
    };

    // Get coefficients: [intercept, beta1, beta2, ...]
    // The intercept is accessed separately via fitted.intercept()
    // The coefficients() method only returns the beta coefficients
    let intercept = fitted.intercept().unwrap_or(0.0);
    let coeffs_col = fitted.coefficients();
    let mut coeffs = vec![intercept];
    for i in 0..coeffs_col.nrows() {
        coeffs.push(coeffs_col[i]);
    }

    // Calculate residuals: y - y_hat
    let predictions = fitted.predict(&x_mat);
    let residuals: Vec<f64> = (0..n).map(|i| y[i] - predictions[i]).collect();

    (coeffs, residuals)
}

/// Apply regression coefficients to future X values
fn apply_regression(coeffs: &[f64], future_x: &[Vec<f64>], horizon: usize) -> Vec<f64> {
    if coeffs.is_empty() || future_x.is_empty() {
        return vec![0.0; horizon];
    }

    // coeffs format: [intercept, beta1, beta2, ...]
    let intercept = coeffs[0];
    let betas = &coeffs[1..];

    (0..horizon)
        .map(|h| {
            let mut effect = intercept;
            for (j, beta) in betas.iter().enumerate() {
                if j < future_x.len() && h < future_x[j].len() {
                    effect += beta * future_x[j][h];
                }
            }
            effect
        })
        .collect()
}

/// ARIMA forecast with exogenous variables (ARIMAX).
///
/// Approach: Regress y on X, then forecast the residuals with ARIMA,
/// and add back the exogenous effect for forecast horizon.
fn forecast_arima_with_exog(
    values: &[f64],
    horizon: usize,
    exog: &ExogenousData,
) -> Result<ForecastOutput> {
    // Fit regression: y = X*beta + residuals
    let (coeffs, residuals) = fit_ols_regression(values, &exog.historical);

    // Forecast residuals with AutoARIMA (D-08/D-09, plan 10-02: `forecast_arima`
    // now requires an explicit `arima_order` — there is none to hand-pick here,
    // and guessing one would reintroduce exactly the kind of fixed-stand-in this
    // plan removes). `forecast_auto_arima`'s own order search needs no such
    // input; `level`/`period` are otherwise inert here since this function's
    // output never surfaces the residual model's own interval/seasonal fields
    // (see the hardcoded empty `lower`/`upper` below). Native crate exog
    // routing (`AutoARIMA::predict_with_exog_intervals`, D-10) replaces this
    // whole OLS + residual-model hack in a later plan.
    let residual_forecast = forecast_auto_arima(&residuals, horizon, 0, 0.95)?;

    // Calculate exogenous effect for future
    let exog_effect = apply_regression(&coeffs, &exog.future, horizon);

    // Combine: forecast = residual_forecast + exog_effect
    let point: Vec<f64> = residual_forecast
        .point
        .iter()
        .zip(exog_effect.iter())
        .map(|(r, e)| r + e)
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "ARIMAX".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

/// Theta forecast with exogenous variables.
///
/// Approach: Similar to ARIMAX - regress, forecast residuals, add back exog effect.
fn forecast_theta_with_exog(
    values: &[f64],
    horizon: usize,
    exog: &ExogenousData,
) -> Result<ForecastOutput> {
    // Fit regression
    let (coeffs, residuals) = fit_ols_regression(values, &exog.historical);

    // Forecast residuals with Theta (STM for exog path). Level is irrelevant here:
    // this function's output always carries empty lower/upper (see below), and the
    // residual_forecast's own bounds are never read.
    let residual_forecast = forecast_theta_stm(&residuals, horizon, 1, 0.95)?;

    // Calculate exogenous effect for future
    let exog_effect = apply_regression(&coeffs, &exog.future, horizon);

    // Combine
    let point: Vec<f64> = residual_forecast
        .point
        .iter()
        .zip(exog_effect.iter())
        .map(|(r, e)| r + e)
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "ThetaX".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

/// MFLES forecast with exogenous variables.
fn forecast_mfles_with_exog(
    values: &[f64],
    horizon: usize,
    periods: &[usize],
    exog: &ExogenousData,
) -> Result<ForecastOutput> {
    // Fit regression
    let (coeffs, residuals) = fit_ols_regression(values, &exog.historical);

    // Forecast residuals with MFLES. Level is irrelevant here: this function's
    // output always carries empty lower/upper (see below), and the
    // residual_forecast's own bounds are never read.
    let residual_forecast = forecast_mfles(&residuals, horizon, periods, 0.95)?;

    // Calculate exogenous effect for future
    let exog_effect = apply_regression(&coeffs, &exog.future, horizon);

    // Combine
    let point: Vec<f64> = residual_forecast
        .point
        .iter()
        .zip(exog_effect.iter())
        .map(|(r, e)| r + e)
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "MFLESX".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

/// Check if model is an auto-selection model
#[cfg(test)]
fn is_auto_model(model: ModelType) -> bool {
    matches!(
        model,
        ModelType::AutoETS
            | ModelType::AutoARIMA
            | ModelType::AutoTheta
            | ModelType::AutoMFLES
            | ModelType::AutoMSTL
            | ModelType::AutoTBATS
    )
}

#[cfg(test)]
fn select_best_model(values: &[f64], period: usize) -> ModelType {
    // Simple model selection based on series characteristics
    let n = values.len();

    if n < 10 {
        return ModelType::Naive;
    }

    // Check for seasonality
    let has_seasonality = period > 1 && n >= 2 * period;

    // Check for trend
    let first_half_mean = values[..n / 2].iter().sum::<f64>() / (n / 2) as f64;
    let second_half_mean = values[n / 2..].iter().sum::<f64>() / (n - n / 2) as f64;
    let has_trend = (second_half_mean - first_half_mean).abs()
        > (values
            .iter()
            .map(|v| (v - first_half_mean).abs())
            .sum::<f64>()
            / n as f64)
            * 0.5;

    match (has_trend, has_seasonality) {
        (true, true) => ModelType::HoltWinters,
        (true, false) => ModelType::Holt,
        (false, true) => ModelType::SeasonalNaive,
        (false, false) => ModelType::SES,
    }
}

/// List all available model names (35 models matching C++ extension).
/// See: <https://github.com/DataZooDE/anofox-forecast/blob/main/docs/API_REFERENCE.md#supported-models>
pub fn list_models() -> Vec<String> {
    vec![
        // Automatic Selection Models (6)
        "AutoETS",
        "AutoARIMA",
        "AutoTheta",
        "AutoMFLES",
        "AutoMSTL",
        "AutoTBATS",
        // Basic Models (6)
        "Naive",
        "SMA",
        "SeasonalNaive",
        "SES",
        "SESOptimized",
        "RandomWalkDrift",
        // Exponential Smoothing Models (5)
        "Holt",
        "HoltWinters",
        "SeasonalES",
        "SeasonalESOptimized",
        "SeasonalWindowAverage",
        // Theta Methods (5) - AutoTheta counted above
        "Theta",
        "OptimizedTheta",
        "DynamicTheta",
        "DynamicOptimizedTheta",
        // State Space Models (2) - AutoETS counted above
        "ETS",
        // ARIMA Models (2) - AutoARIMA counted above
        "ARIMA",
        // Multiple Seasonality Models (6) - Auto variants counted above
        "MFLES",
        "MSTL",
        "TBATS",
        // Intermittent Demand Models (6)
        "CrostonClassic",
        "CrostonOptimized",
        "CrostonSBA",
        "ADIDA",
        "IMAPA",
        "TSB",
        // Distributional Models (1)
        "Laplace",
        // Classical Models (2) — Phase 3 additions
        "GARCH",
        "Kalman",
    ]
    .into_iter()
    .map(String::from)
    .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// DEP-01 proof: `fit_ols_regression` is private, so this test is
    /// inline (09-RESEARCH Open Question 2 — no visibility change) rather
    /// than a `tests/` integration test. Loads
    /// `multiscale_ols/cases.json` via the crate's own `test_fixtures`
    /// loader module (`#[path]`'d from `tests/common/mod.rs` in lib.rs).
    #[test]
    fn fit_ols_regression_matches_numpy_lstsq_fixture() {
        let fx = crate::test_fixtures::load_fixture("multiscale_ols/cases.json");
        let coef_abs = crate::test_fixtures::tolerance(&fx, "coef_abs");
        let coef_rel = crate::test_fixtures::tolerance(&fx, "coef_rel");
        let fitted_abs_per_sd = crate::test_fixtures::tolerance(&fx, "fitted_abs_per_sd");

        let cases = fx["cases"]
            .as_object()
            .expect("multiscale_ols/cases.json: 'cases' is not an object");
        let mut ran_exog_typical = false;
        let mut asserted_count = 0usize;

        for (name, case) in cases {
            let dep01_assert = case["dep01_assert"].as_bool().unwrap_or(false);
            if !dep01_assert {
                continue;
            }
            asserted_count += 1;

            let x_cols: Vec<Vec<f64>> = case["X"]
                .as_array()
                .unwrap_or_else(|| panic!("{name}: 'X' is not an array"))
                .iter()
                .map(crate::test_fixtures::f64_vec)
                .collect();
            let y = crate::test_fixtures::f64_vec(&case["y"]);
            let want_coef = crate::test_fixtures::f64_vec(&case["coef"]);
            let want_fitted = crate::test_fixtures::f64_vec(&case["fitted"]);
            let full_rank = case["full_rank"].as_bool().unwrap_or(false);

            let (coeffs, residuals) = fit_ols_regression(&y, &x_cols);

            // Full-rank designs: coefficients (intercept first) must match
            // numpy.linalg.lstsq within the fixture's coef tolerance.
            if full_rank {
                crate::test_fixtures::assert_vec_close(
                    &coeffs,
                    &want_coef,
                    coef_abs,
                    coef_rel,
                    &format!("{name}.coef"),
                );
            }

            // Every dep01_assert case (full-rank or not): the fitted
            // projection (y - residual) must match numpy's fitted values
            // within fitted_abs_per_sd * sd(y).
            assert!(
                y.len() > 1,
                "{name}: need at least 2 observations to compute var_y"
            );
            let mean_y = y.iter().sum::<f64>() / y.len() as f64;
            let var_y =
                y.iter().map(|v| (v - mean_y).powi(2)).sum::<f64>() / (y.len() as f64 - 1.0);
            let fit_tol = fitted_abs_per_sd * var_y.sqrt();

            for i in 0..y.len() {
                let fitted_i = y[i] - residuals[i];
                crate::test_fixtures::assert_close(
                    fitted_i,
                    want_fitted[i],
                    fit_tol,
                    0.0,
                    &format!("{name}.fitted[{i}]"),
                );
            }

            // exog_typical additionally proves apply_regression() on the
            // first 5 future rows reproduces [1, x] . coef — this is the
            // audit's exact before/after evidence: on anofox-regression
            // 0.5.3 the scrambled coefficients make step 1 ~ -79; on
            // >=0.5.13 it is ~ -8.7987.
            if name == "exog_typical" {
                ran_exog_typical = true;
                let future_x: Vec<Vec<f64>> = x_cols.iter().map(|col| col[0..5].to_vec()).collect();
                let projected = apply_regression(&coeffs, &future_x, 5);

                let want_projected: Vec<f64> = (0..5)
                    .map(|h| {
                        let mut v = want_coef[0];
                        for (j, col) in future_x.iter().enumerate() {
                            v += want_coef[j + 1] * col[h];
                        }
                        v
                    })
                    .collect();

                crate::test_fixtures::assert_vec_close(
                    &projected,
                    &want_projected,
                    fit_tol,
                    0.0,
                    "exog_typical.apply_regression",
                );
                assert!(
                    (projected[0] - (-8.7987)).abs() < 1e-3,
                    "exog_typical.apply_regression[0]: got {}, want approx -8.7987 \
                     (audit fix evidence; 0.5.3 scrambled this to ~ -79)",
                    projected[0]
                );
            }
        }

        assert!(
            ran_exog_typical,
            "multiscale_ols/cases.json: missing the exog_typical case"
        );
        assert_eq!(
            asserted_count, 7,
            "multiscale_ols/cases.json: expected 7 dep01_assert cases to run, ran {asserted_count}"
        );

        // NOTE: rank-deficient designs (collinear/collinear_big/constcol)
        // leave aliased coefficients NaN in anofox-regression >=0.5.13, and
        // apply_regression then produces NaN effects for the aliased
        // column; the finite zero-effect behaviour is Phase 10 FCST-05 —
        // this test pins only the fitted projection for dep01_assert
        // cases, not the NaN-aliasing behaviour itself.
    }

    #[test]
    fn test_forecast_naive() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0), Some(5.0)];
        let options = ForecastOptions {
            model: ModelType::Naive,
            horizon: 3,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 3);
        assert_eq!(result.point[0], 5.0);
    }

    #[test]
    fn test_forecast_ses() {
        let values: Vec<Option<f64>> = (0..20).map(|i| Some(i as f64)).collect();
        let options = ForecastOptions {
            model: ModelType::SES,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
    }

    #[test]
    fn test_forecast_holt_winters() {
        // Create seasonal data: base + seasonal pattern
        let values: Vec<Option<f64>> = (0..48)
            .map(|i| {
                let seasonal = (i % 12) as f64 * 2.0; // period 12
                let trend = i as f64 * 0.5;
                Some(100.0 + trend + seasonal)
            })
            .collect();

        let options = ForecastOptions {
            model: ModelType::HoltWinters,
            horizon: 12,
            seasonal_period: 12,
            auto_detect_seasonality: false,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 12);
        assert_eq!(result.model_name, "HoltWinters");
        // Forecasts should be reasonable (positive and not NaN)
        assert!(result.point.iter().all(|v| v.is_finite() && *v > 0.0));
    }

    #[test]
    fn test_forecast_arima() {
        // Create trending data with some noise
        let values: Vec<Option<f64>> = (0..30)
            .map(|i| Some(100.0 + i as f64 * 2.0 + (i % 3) as f64))
            .collect();

        let options = ForecastOptions {
            model: ModelType::ARIMA,
            horizon: 5,
            arima_order: Some([1, 1, 1]),
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
        // The crate may append " with drift" (d+D==1, AICc-selected nonzero
        // intercept) or " with non-zero mean" (d+D==0) per its own fit —
        // only the base "ARIMA(p,d,q)" prefix is asserted here.
        assert!(
            result.model_name.starts_with("ARIMA(1,1,1)"),
            "expected model_name to start with 'ARIMA(1,1,1)', got '{}'",
            result.model_name
        );
        // ARIMA forecasts should continue the trend
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_arima_without_order_errors() {
        // D-09: ARIMA without arima_order is a hard error naming AutoARIMA,
        // never a silent fallback to a different model.
        let values: Vec<Option<f64>> = (0..30)
            .map(|i| Some(100.0 + i as f64 * 2.0 + (i % 3) as f64))
            .collect();

        let options = ForecastOptions {
            model: ModelType::ARIMA,
            horizon: 5,
            ..Default::default()
        };

        let err = forecast(&values, &options).unwrap_err();
        let msg = err.to_string();
        assert!(
            msg.contains("arima_order") && msg.contains("AutoARIMA"),
            "expected the no-order ARIMA error to name arima_order and AutoARIMA, got: {msg}"
        );
    }

    #[test]
    fn test_forecast_mstl() {
        // Create data with trend and seasonality
        let values: Vec<Option<f64>> = (0..36)
            .map(|i| {
                let seasonal = ((i % 6) as f64 * std::f64::consts::PI / 3.0).sin() * 10.0;
                Some(50.0 + i as f64 + seasonal)
            })
            .collect();

        let options = ForecastOptions {
            model: ModelType::MSTL,
            horizon: 6,
            seasonal_period: 6,
            auto_detect_seasonality: false,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 6);
        assert_eq!(result.model_name, "MSTL");
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_tbats() {
        let values: Vec<Option<f64>> = (0..24)
            .map(|i| Some(100.0 + (i % 4) as f64 * 5.0))
            .collect();

        let options = ForecastOptions {
            model: ModelType::TBATS,
            horizon: 4,
            seasonal_period: 4,
            auto_detect_seasonality: false,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 4);
        assert_eq!(result.model_name, "TBATS");
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_croston() {
        // Intermittent demand: many zeros with occasional non-zero values
        let values: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(0.0),
            Some(5.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(3.0),
            Some(0.0),
            Some(4.0),
            Some(0.0),
            Some(0.0),
            Some(6.0),
        ];

        let options = ForecastOptions {
            model: ModelType::CrostonClassic,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
        assert_eq!(result.model_name, "CrostonClassic");
        // Croston produces flat forecasts
        assert!(result.point.iter().all(|v| v.is_finite() && *v > 0.0));
        // All forecast values should be the same
        let first = result.point[0];
        assert!(result.point.iter().all(|v| (*v - first).abs() < 1e-10));
    }

    /// Regression test for #167: every ModelType must return a model_name that
    /// either equals or starts with model.name(). Guards against shared helpers
    /// hardcoding a wrong model name.
    #[test]
    fn test_all_model_names_match_enum() {
        // Intermittent demand data (many zeros)
        let intermittent: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(0.0),
            Some(5.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(3.0),
            Some(0.0),
            Some(4.0),
            Some(0.0),
            Some(0.0),
            Some(6.0),
        ];

        // Trend + seasonality data (60 points, period ~7)
        let seasonal: Vec<Option<f64>> = (0..60)
            .map(|i| {
                Some(10.0 + i as f64 * 0.5 + (i as f64 * std::f64::consts::PI / 7.0).sin() * 3.0)
            })
            .collect();

        // Models that now require a resolved seasonal period (D-07) are
        // given an explicit seasonal_period (matching `seasonal`'s true
        // embedded period of 7) rather than relying on auto-detection —
        // this test asserts naming conventions, not detector accuracy
        // (detector tuning is Phase 15). `0` means "leave auto-detect on".
        let seasonal_period_for = |model: ModelType| -> usize {
            if requires_seasonal_period(model) {
                7
            } else {
                0
            }
        };

        // Non-auto models: model_name must equal model.name() exactly
        let exact_cases: Vec<(ModelType, &[Option<f64>])> = vec![
            // Basic models
            (ModelType::Naive, &seasonal),
            (ModelType::SMA, &seasonal),
            (ModelType::SeasonalNaive, &seasonal),
            (ModelType::SES, &seasonal),
            (ModelType::SESOptimized, &seasonal),
            (ModelType::RandomWalkDrift, &seasonal),
            // Exponential Smoothing
            (ModelType::Holt, &seasonal),
            (ModelType::HoltWinters, &seasonal),
            (ModelType::SeasonalES, &seasonal),
            (ModelType::SeasonalESOptimized, &seasonal),
            (ModelType::SeasonalWindowAverage, &seasonal),
            // Theta variants
            (ModelType::Theta, &seasonal),
            (ModelType::OptimizedTheta, &seasonal),
            (ModelType::DynamicTheta, &seasonal),
            (ModelType::DynamicOptimizedTheta, &seasonal),
            // State Space: ETS is excluded here (D-05) — without an explicit
            // spec it now runs the crate AutoETS selection and reports the
            // chosen spec (e.g. "ETS(M,A,N)"), never the bare enum name;
            // covered below in prefix_cases.
            // ARIMA is excluded here (D-09): it now requires an explicit
            // arima_order and reports "ARIMA(p,d,q)" (and "(P,D,Q)[s]" when
            // seasonal), never the bare enum name — covered separately by
            // test_forecast_arima / test_forecast_arima_without_order_errors.
            // Multiple Seasonality
            (ModelType::MFLES, &seasonal),
            (ModelType::MSTL, &seasonal),
            (ModelType::TBATS, &seasonal),
            // Intermittent Demand
            (ModelType::CrostonClassic, &intermittent),
            (ModelType::CrostonOptimized, &intermittent),
            (ModelType::CrostonSBA, &intermittent),
            (ModelType::ADIDA, &intermittent),
            (ModelType::IMAPA, &intermittent),
            (ModelType::TSB, &intermittent),
        ];

        for (model_type, data) in &exact_cases {
            let expected = model_type.name();
            let sp = seasonal_period_for(*model_type);
            let options = ForecastOptions {
                model: *model_type,
                horizon: 3,
                seasonal_period: sp,
                auto_detect_seasonality: sp == 0,
                ..Default::default()
            };
            let result = forecast(data, &options).unwrap();
            assert_eq!(
                result.model_name, expected,
                "{:?} returned model_name='{}', expected '{}'",
                model_type, result.model_name, expected
            );
        }

        // D-06: AutoMFLES/AutoMSTL are documented, deprecated input aliases —
        // model_name always reports the non-Auto family name exactly
        // ("MFLES"/"MSTL"), never the "Auto" enum name.
        let alias_cases: Vec<(ModelType, &str, &[Option<f64>])> = vec![
            (ModelType::AutoMFLES, "MFLES", &seasonal),
            (ModelType::AutoMSTL, "MSTL", &seasonal),
        ];
        for (model_type, expected, data) in &alias_cases {
            let sp = seasonal_period_for(*model_type);
            let options = ForecastOptions {
                model: *model_type,
                horizon: 3,
                seasonal_period: sp,
                auto_detect_seasonality: sp == 0,
                ..Default::default()
            };
            let result = forecast(data, &options).unwrap();
            assert_eq!(
                result.model_name, *expected,
                "{:?} returned model_name='{}', expected exactly '{}' (D-06 alias)",
                model_type, result.model_name, expected
            );
        }

        // Auto models: model_name must start with model.name()
        // (they append details like "AutoARIMA(1,0,0)" or "AutoETS(A,N,N)")
        let prefix_cases: Vec<(ModelType, &[Option<f64>])> = vec![
            // ETS (no spec) reports the crate's selected spec, e.g. "ETS(M,A,N)" (D-05).
            (ModelType::ETS, &seasonal),
            (ModelType::AutoETS, &seasonal),
            (ModelType::AutoARIMA, &seasonal),
            (ModelType::AutoTheta, &seasonal),
            // AutoTBATS: D-07 requires a resolved period just like TBATS.
            (ModelType::AutoTBATS, &seasonal),
            // Laplace's model_name is `Laplace(<variant>)` or
            // `Laplace(<variant>,seasonal=<p>)`; must start with "Laplace".
            (ModelType::Laplace, &seasonal),
        ];

        for (model_type, data) in &prefix_cases {
            let prefix = model_type.name();
            let sp = seasonal_period_for(*model_type);
            let options = ForecastOptions {
                model: *model_type,
                horizon: 3,
                seasonal_period: sp,
                auto_detect_seasonality: sp == 0,
                ..Default::default()
            };
            let result = forecast(data, &options).unwrap();
            assert!(
                result.model_name.starts_with(prefix),
                "{:?} returned model_name='{}', expected prefix '{}'",
                model_type,
                result.model_name,
                prefix
            );
        }
    }

    #[test]
    fn test_forecast_theta() {
        let values: Vec<Option<f64>> = (0..20).map(|i| Some(10.0 + i as f64 * 1.5)).collect();

        let options = ForecastOptions {
            model: ModelType::Theta,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
        assert_eq!(result.model_name, "Theta");
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_laplace_variants() {
        // Trend + weekly-ish seasonality series that all three Laplace
        // variants can handle in a smoke test.
        let values: Vec<Option<f64>> = (0..80)
            .map(|i| {
                let trend = i as f64 * 0.1;
                let seasonal = (i as f64 * std::f64::consts::PI / 7.0).sin() * 2.0;
                Some(10.0 + trend + seasonal)
            })
            .collect();

        for (variant, tag) in [
            (LaplaceVariant::Auto, "auto"),
            (LaplaceVariant::AutoAid, "auto_aid"),
            (LaplaceVariant::Skaters, "skaters"),
        ] {
            let options = ForecastOptions {
                model: ModelType::Laplace,
                horizon: 6,
                laplace_variant: Some(variant),
                ..Default::default()
            };

            let result = forecast(&values, &options).expect("Laplace forecast should succeed");
            assert_eq!(result.point.len(), 6, "variant={tag}");
            assert!(
                result.point.iter().all(|v| v.is_finite()),
                "variant={tag} produced non-finite point forecasts"
            );
            assert!(
                result.model_name.starts_with("Laplace("),
                "variant={tag} model_name={} should start with 'Laplace('",
                result.model_name
            );
            assert!(
                result.model_name.contains(tag),
                "variant={tag} model_name={} should contain '{tag}'",
                result.model_name
            );
        }
    }

    #[test]
    fn test_forecast_laplace_seasonal_batch_init_in_name() {
        // Toggle the 0.15.1 opt-in flag; verify it round-trips into model_name.
        let values: Vec<Option<f64>> = (0..60)
            .map(|i| Some(10.0 + (i as f64 * std::f64::consts::PI / 7.0).sin() * 5.0))
            .collect();

        let options = ForecastOptions {
            model: ModelType::Laplace,
            horizon: 5,
            seasonal_period: 7,
            auto_detect_seasonality: false,
            laplace_variant: Some(LaplaceVariant::Auto),
            laplace_seasonal_batch_init: true,
            ..Default::default()
        };

        let result = forecast(&values, &options).expect("Laplace batch_init should succeed");
        assert!(
            result.model_name.contains("batch_init"),
            "expected batch_init tag in model_name, got '{}'",
            result.model_name
        );
        assert!(
            result.model_name.contains("seasonal=7"),
            "expected seasonal tag in model_name, got '{}'",
            result.model_name
        );
    }

    #[test]
    fn test_forecast_laplace_seasonal_period_in_name() {
        let values: Vec<Option<f64>> = (0..60)
            .map(|i| Some(10.0 + (i as f64 * std::f64::consts::PI / 7.0).sin() * 5.0))
            .collect();

        let options = ForecastOptions {
            model: ModelType::Laplace,
            horizon: 5,
            seasonal_period: 7,
            auto_detect_seasonality: false,
            laplace_variant: Some(LaplaceVariant::Auto),
            ..Default::default()
        };

        let result = forecast(&values, &options).expect("seasonal Laplace should succeed");
        assert!(
            result.model_name.contains("seasonal=7"),
            "expected seasonal tag in model_name, got '{}'",
            result.model_name
        );
    }

    #[test]
    fn test_laplace_variant_parse_roundtrip() {
        assert_eq!(LaplaceVariant::parse("").unwrap(), LaplaceVariant::Auto);
        assert_eq!(LaplaceVariant::parse("auto").unwrap(), LaplaceVariant::Auto);
        assert_eq!(
            LaplaceVariant::parse("Auto_Aid").unwrap(),
            LaplaceVariant::AutoAid
        );
        assert_eq!(
            LaplaceVariant::parse("AID").unwrap(),
            LaplaceVariant::AutoAid
        );
        assert_eq!(
            LaplaceVariant::parse("skaters").unwrap(),
            LaplaceVariant::Skaters
        );
        assert!(LaplaceVariant::parse("bogus").is_err());
    }

    #[test]
    fn test_forecast_auto_ets() {
        let values: Vec<Option<f64>> = (0..30).map(|i| Some(50.0 + (i % 7) as f64 * 3.0)).collect();

        let options = ForecastOptions {
            model: ModelType::AutoETS,
            horizon: 7,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 7);
        // AutoETS selects a model automatically
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_auto_ets_constant_series() {
        // A constant series used to trigger NaN in the anofox-forecast
        // optimizer (issue #192); anofox-forecast 0.15.10 fits it directly
        // without panicking, so this exercises the normal Ok(Ok(..)) path,
        // not the catch_unwind safety net. D-07 (plan 10-03): if the crate
        // ever does fail/panic on this input again, forecast_auto_ets_core
        // now returns a hard InvalidInput naming AutoETS — never a silent
        // fallback to a different model — so this test would then need to
        // assert on that error instead.
        let values: Vec<Option<f64>> = vec![Some(42.0); 30];

        let options = ForecastOptions {
            model: ModelType::AutoETS,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
        assert!(result.point.iter().all(|v| v.is_finite()));
        // Forecasts should be close to the constant value
        assert!(
            result.point.iter().all(|v| (v - 42.0).abs() < 1.0),
            "Expected forecasts near 42.0, got {:?}",
            result.point
        );
    }

    #[test]
    fn test_select_best_model_with_trend_and_seasonality() {
        // Data with both trend and seasonality
        let values: Vec<f64> = (0..48)
            .map(|i| {
                let trend = i as f64 * 2.0;
                let seasonal = (i % 12) as f64 * 3.0;
                100.0 + trend + seasonal
            })
            .collect();

        let model = select_best_model(&values, 12);
        assert_eq!(model, ModelType::HoltWinters);
    }

    #[test]
    fn test_select_best_model_short_series() {
        let values: Vec<f64> = vec![1.0, 2.0, 3.0];
        let model = select_best_model(&values, 1);
        assert_eq!(model, ModelType::Naive);
    }

    #[test]
    fn test_is_auto_model() {
        assert!(is_auto_model(ModelType::AutoETS));
        assert!(is_auto_model(ModelType::AutoARIMA));
        assert!(is_auto_model(ModelType::AutoTheta));
        assert!(is_auto_model(ModelType::AutoMFLES));
        assert!(is_auto_model(ModelType::AutoMSTL));
        assert!(is_auto_model(ModelType::AutoTBATS));
        assert!(!is_auto_model(ModelType::Naive));
        assert!(!is_auto_model(ModelType::HoltWinters));
    }

    #[test]
    fn test_model_type_from_str() {
        assert_eq!("Naive".parse::<ModelType>().unwrap(), ModelType::Naive);
        assert_eq!("naive".parse::<ModelType>().unwrap(), ModelType::Naive);
        assert_eq!(
            "HoltWinters".parse::<ModelType>().unwrap(),
            ModelType::HoltWinters
        );
        assert_eq!("hw".parse::<ModelType>().unwrap(), ModelType::HoltWinters);
        assert_eq!(
            "croston".parse::<ModelType>().unwrap(),
            ModelType::CrostonClassic
        );
        assert!("invalid_model".parse::<ModelType>().is_err());
    }

    #[test]
    fn test_forecast_with_nulls() {
        // Test that NULL values are handled via interpolation
        let values: Vec<Option<f64>> = vec![
            Some(1.0),
            Some(2.0),
            None,
            Some(4.0),
            Some(5.0),
            None,
            Some(7.0),
        ];

        let options = ForecastOptions {
            model: ModelType::Naive,
            horizon: 3,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 3);
        assert!(result.point.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn test_forecast_with_fitted_and_residuals() {
        let values: Vec<Option<f64>> = (0..15).map(|i| Some(i as f64 * 2.0)).collect();

        let options = ForecastOptions {
            model: ModelType::SES,
            horizon: 3,
            include_fitted: true,
            include_residuals: true,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert!(result.fitted.is_some());
        assert!(result.residuals.is_some());
        assert_eq!(result.fitted.as_ref().unwrap().len(), 15);
        assert_eq!(result.residuals.as_ref().unwrap().len(), 15);
        assert!(result.mse.is_some());
    }

    #[test]
    fn test_insufficient_data() {
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let options = ForecastOptions::default();

        let result = forecast(&values, &options);
        assert!(result.is_err());
    }

    #[test]
    fn test_is_valid_ets_notation() {
        // Valid 3-character notations
        assert!(is_valid_ets_notation("AAA"));
        assert!(is_valid_ets_notation("AAN"));
        assert!(is_valid_ets_notation("AAM"));
        assert!(is_valid_ets_notation("ANA"));
        assert!(is_valid_ets_notation("ANN"));
        assert!(is_valid_ets_notation("ANM"));
        assert!(is_valid_ets_notation("AMA"));
        assert!(is_valid_ets_notation("AMN"));
        assert!(is_valid_ets_notation("AMM"));
        assert!(is_valid_ets_notation("MAA"));
        assert!(is_valid_ets_notation("MNM"));
        assert!(is_valid_ets_notation("MMM"));

        // Valid 4-character damped notations
        assert!(is_valid_ets_notation("AAdA"));
        assert!(is_valid_ets_notation("AAdN"));
        assert!(is_valid_ets_notation("AAdM"));
        assert!(is_valid_ets_notation("MAdA"));
        assert!(is_valid_ets_notation("MAdM"));
        assert!(is_valid_ets_notation("MMdA"));

        // Invalid notations
        assert!(!is_valid_ets_notation("XYZ")); // Invalid characters
        assert!(!is_valid_ets_notation("123")); // Numbers
        assert!(!is_valid_ets_notation("AA")); // Too short
        assert!(!is_valid_ets_notation("AAAAA")); // Too long
        assert!(!is_valid_ets_notation("AAxA")); // Invalid damping char
        assert!(!is_valid_ets_notation("NAA")); // N not valid for error type
        assert!(!is_valid_ets_notation("")); // Empty
        assert!(!is_valid_ets_notation("aaa")); // Lowercase
    }

    #[test]
    fn test_ets_invalid_spec_returns_error() {
        // Test that an invalid ETS spec returns an error
        let values: Vec<Option<f64>> = (0..20).map(|i| Some(100.0 + i as f64)).collect();

        let options = ForecastOptions {
            model: ModelType::ETS,
            ets_spec: Some("XYZ".to_string()),
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options);
        assert!(result.is_err(), "Expected error for invalid ETS spec 'XYZ'");

        let err = result.unwrap_err();
        let err_msg = err.to_string();
        assert!(
            err_msg.contains("Invalid ETS model specification"),
            "Error message should mention invalid ETS spec: {}",
            err_msg
        );
    }

    #[test]
    fn test_ets_explicit_spec_computation_error_not_input_error() {
        // When user explicitly requests a valid ETS spec and the underlying library
        // fails to fit, return ComputationError (group skipped, null forecast)
        // rather than InvalidInput (which would abort the pipeline).
        // Use seasonal spec "AAA" with period=12 but only 5 data points — too few
        // for a seasonal model to fit.
        let values: Vec<Option<f64>> = (0..5).map(|i| Some(100.0 + i as f64)).collect();

        let options = ForecastOptions {
            model: ModelType::ETS,
            ets_spec: Some("AAA".to_string()),
            seasonal_period: 12,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options);
        assert!(result.is_err());
        let err = result.unwrap_err();
        // Must be ComputationError (skippable), not InvalidInput (pipeline-aborting)
        assert_eq!(
            err.to_code(),
            3,
            "Expected ComputationError (code 3), got code {} for: {}",
            err.to_code(),
            err
        );
    }

    #[test]
    fn test_ets_without_spec_falls_back() {
        // ETS without explicit spec should still fall back to simplified implementation
        let values: Vec<Option<f64>> = (0..60).map(|i| Some(100.0 + i as f64)).collect();

        let options = ForecastOptions {
            model: ModelType::ETS,
            ets_spec: None,
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options);
        assert!(
            result.is_ok(),
            "ETS without explicit spec should fall back: {:?}",
            result.err()
        );
    }

    #[test]
    fn test_auto_arima_uses_proper_library() {
        // Test that AutoARIMA uses the anofox-forecast library's AutoARIMA implementation
        // which does actual AIC-based model selection, not the simplified ARIMA(1,1,1)
        let values: Vec<Option<f64>> = (0..60)
            .map(|i| Some(100.0 + i as f64 * 0.5 + 10.0 * (i as f64 * 0.1).sin()))
            .collect();

        let options = ForecastOptions {
            model: ModelType::AutoARIMA,
            horizon: 5,
            seasonal_period: 12,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();

        // Verify the model name indicates AutoARIMA was used (not Naive or SeasonalNaive)
        assert!(
            result.model_name.starts_with("AutoARIMA"),
            "Expected model_name to start with 'AutoARIMA', got '{}'",
            result.model_name
        );

        // Verify we got forecasts
        assert_eq!(result.point.len(), 5);
        assert!(result.point.iter().all(|v| v.is_finite()));

        // The proper AutoARIMA should include the selected (p,d,q) order in the name
        // Format: "AutoARIMA(p,d,q)" or "AutoARIMA(p,d,q)(P,D,Q)[s]" for seasonal
        println!("AutoARIMA model name: {}", result.model_name);
    }

    #[test]
    fn test_auto_ets_uses_proper_library() {
        // Test that AutoETS uses the anofox-forecast library's AutoETS implementation
        // which does actual AICc-based model selection
        let values: Vec<Option<f64>> = (0..48)
            .map(|i| {
                Some(
                    100.0
                        + i as f64 * 0.5
                        + 10.0 * (2.0 * std::f64::consts::PI * i as f64 / 12.0).sin(),
                )
            })
            .collect();

        let options = ForecastOptions {
            model: ModelType::AutoETS,
            horizon: 5,
            seasonal_period: 12,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();

        // Verify the model name indicates AutoETS was used (not SeasonalNaive)
        assert!(
            result.model_name.starts_with("AutoETS"),
            "Expected model_name to start with 'AutoETS', got '{}'",
            result.model_name
        );

        // Verify we got forecasts
        assert_eq!(result.point.len(), 5);
        assert!(result.point.iter().all(|v| v.is_finite()));

        // The proper AutoETS should include the selected (E,T,S) spec in the name
        // Format: "AutoETS(Error,Trend,Seasonal)"
        println!("AutoETS model name: {}", result.model_name);
    }

    #[test]
    fn test_auto_arima_different_from_explicit_arima() {
        // D-09: ARIMA now requires an explicit arima_order and fits a real
        // crate model (no more fixed-coefficient AR(1) stand-in) — verify
        // AutoARIMA's own order search still reports a distinct model_name
        // from an explicitly-ordered ARIMA(1,1,1) on the same data.
        let values: Vec<Option<f64>> = (0..50)
            .map(|i| Some(100.0 + i as f64 * 2.0 + 5.0 * (i as f64 * 0.2).sin()))
            .collect();

        let auto_options = ForecastOptions {
            model: ModelType::AutoARIMA,
            horizon: 5,
            ..Default::default()
        };

        let explicit_options = ForecastOptions {
            model: ModelType::ARIMA,
            horizon: 5,
            arima_order: Some([1, 1, 1]),
            ..Default::default()
        };

        let auto_result = forecast(&values, &auto_options).unwrap();
        let explicit_result = forecast(&values, &explicit_options).unwrap();

        assert!(auto_result.model_name.starts_with("AutoARIMA"));
        assert!(explicit_result.model_name.starts_with("ARIMA(1,1,1)"));

        println!("AutoARIMA forecasts: {:?}", auto_result.point);
        println!(
            "Explicit ARIMA(1,1,1) forecasts: {:?}",
            explicit_result.point
        );
    }

    #[test]
    fn test_mfles_uses_proper_implementation() {
        // Verify MFLES uses the proper anofox-forecast MFLES model
        let values: Vec<Option<f64>> = (0..48)
            .map(|i| {
                let trend = i as f64 * 0.5;
                let seasonal = 10.0 * ((i % 12) as f64 * std::f64::consts::PI / 6.0).sin();
                Some(100.0 + trend + seasonal)
            })
            .collect();

        let options = ForecastOptions {
            model: ModelType::MFLES,
            horizon: 12,
            include_fitted: true,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 12);
        assert_eq!(result.model_name, "MFLES");
        assert!(result.point.iter().all(|v| v.is_finite()));
        assert!(result.fitted.is_some());
    }

    #[test]
    fn test_mfles_high_cv_no_catastrophe() {
        // High coefficient-of-variation series that previously caused catastrophic forecasts
        // Simulates intermittent demand with high variance
        let values: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(0.0),
            Some(5.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(12.0),
            Some(0.0),
            Some(0.0),
            Some(3.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(8.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(15.0),
            Some(0.0),
            Some(0.0),
            Some(1.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(6.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(10.0),
            Some(0.0),
            Some(0.0),
            Some(2.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
        ];

        let options = ForecastOptions {
            model: ModelType::AutoMFLES,
            horizon: 12,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 12);
        // Forecasts should be reasonable — not in the billions
        let max_actual = 15.0;
        for (i, &v) in result.point.iter().enumerate() {
            assert!(v.is_finite(), "Forecast at step {} is not finite: {}", i, v);
            assert!(
                v.abs() < max_actual * 100.0,
                "Forecast at step {} is catastrophic: {} (max actual was {})",
                i,
                v,
                max_actual
            );
        }
    }

    #[test]
    fn test_mfles_intermittent_stable() {
        // Pure intermittent demand — lots of zeros with occasional spikes
        let values: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(1.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(2.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(1.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(3.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
            Some(1.0),
        ];

        let options = ForecastOptions {
            model: ModelType::MFLES,
            horizon: 6,
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 6);
        // All forecasts should be finite and within a reasonable range
        let max_actual = 3.0;
        for (i, &v) in result.point.iter().enumerate() {
            assert!(v.is_finite(), "Forecast at step {} is not finite: {}", i, v);
            assert!(
                v.abs() < max_actual * 100.0,
                "Forecast at step {} diverged: {} (max actual was {})",
                i,
                v,
                max_actual
            );
        }
    }

    // ========================================================================
    // Phase 3: Kalman + GARCH tests
    // ========================================================================

    #[test]
    fn test_forecast_kalman_local_level() {
        // A non-trivial series (30 values with trend + noise pattern)
        let values: Vec<Option<f64>> = (0..30)
            .map(|i| Some(10.0 + i as f64 * 0.5 + (i % 3) as f64))
            .collect();
        let options = ForecastOptions {
            model: ModelType::Kalman,
            horizon: 5,
            ..Default::default()
        };
        let result = forecast(&values, &options).unwrap();
        assert_eq!(
            result.point.len(),
            5,
            "Kalman must return horizon=5 point values"
        );
        assert_eq!(result.model_name, "Kalman(local_level)");
        assert!(
            result.point.iter().all(|v| v.is_finite()),
            "All Kalman forecasts must be finite"
        );
    }

    #[test]
    fn test_forecast_kalman_local_linear_trend() {
        let values: Vec<Option<f64>> = (0..30).map(|i| Some(5.0 + i as f64 * 1.2)).collect();
        let options_ll = ForecastOptions {
            model: ModelType::Kalman,
            kalman_model: None, // local_level
            horizon: 5,
            ..Default::default()
        };
        let options_llt = ForecastOptions {
            model: ModelType::Kalman,
            kalman_model: Some("local_linear_trend".to_string()),
            horizon: 5,
            ..Default::default()
        };
        let result_ll = forecast(&values, &options_ll).unwrap();
        let result_llt = forecast(&values, &options_llt).unwrap();
        assert_eq!(result_ll.point.len(), 5);
        assert_eq!(result_llt.point.len(), 5);
        // The two specs produce different point forecasts on a trended series
        assert_ne!(
            result_ll.point[0], result_llt.point[0],
            "local_level and local_linear_trend should produce different forecasts"
        );
    }

    #[test]
    fn test_forecast_garch_basic() {
        // Returns-like series of length >= 12 (GARCH(1,1) minimum)
        let values: Vec<Option<f64>> = vec![
            Some(0.01),
            Some(-0.02),
            Some(0.03),
            Some(-0.015),
            Some(0.025),
            Some(-0.01),
            Some(0.02),
            Some(-0.03),
            Some(0.015),
            Some(-0.025),
            Some(0.012),
            Some(-0.018),
            Some(0.022),
            Some(-0.011),
            Some(0.028),
        ];
        let options = ForecastOptions {
            model: ModelType::GARCH,
            horizon: 5,
            ..Default::default()
        };
        let result = forecast(&values, &options).unwrap();
        assert_eq!(
            result.point.len(),
            5,
            "GARCH must return horizon=5 point values"
        );
        assert_eq!(result.model_name, "GARCH(1,1)");
        for &v in &result.point {
            assert!(v >= 0.0, "GARCH volatility must be non-negative, got {}", v);
        }
    }

    #[test]
    fn test_forecast_garch_sqrt_of_variance() {
        // Verify that point values equal sqrt of variance (spot-check element 0)
        let values: Vec<f64> = vec![
            0.01, -0.02, 0.03, -0.015, 0.025, -0.01, 0.02, -0.03, 0.015, -0.025, 0.012, -0.018,
            0.022, -0.011, 0.028,
        ];
        let ts = make_timeseries(&values).unwrap();
        let mut model = GARCH::new(1, 1);
        model.fit(&ts).unwrap();
        let variance = model.forecast_variance(5).unwrap();
        let volatility_from_core = forecast_garch(&values, 5, 1, 1, 0.95).unwrap();
        // Element 0: sqrt(variance[0]) should match volatility[0] within 1e-9
        let expected = variance[0].sqrt();
        let actual = volatility_from_core.point[0];
        assert!(
            (actual - expected).abs() < 1e-9,
            "GARCH forecast[0] = {} but expected sqrt(variance[0]) = {}",
            actual,
            expected
        );
    }

    #[test]
    fn test_forecast_garch_insufficient_data() {
        // A series shorter than p+q+10 = 12 for GARCH(1,1) should return Err
        let values: Vec<Option<f64>> = (0..8).map(|i| Some(i as f64 * 0.01)).collect();
        let options = ForecastOptions {
            model: ModelType::GARCH,
            horizon: 3,
            ..Default::default()
        };
        let result = forecast(&values, &options);
        assert!(
            result.is_err(),
            "GARCH on a series with 8 obs (< 12 min for GARCH(1,1)) should return Err"
        );
    }

    // -------------------------------------------------------------------------
    // parse_combination_method (IN-02)
    // -------------------------------------------------------------------------

    #[test]
    fn parse_combination_method_rejects_unknown() {
        assert!(
            parse_combination_method(Some("custom")).is_err(),
            "'custom' should be rejected"
        );
        assert!(
            parse_combination_method(Some("bad_value")).is_err(),
            "unknown string should be rejected"
        );
    }

    #[test]
    fn parse_combination_method_accepts_canonical_strings() {
        // Six canonical strings must parse without error
        for m in &[
            "mean",
            "median",
            "weighted_mse",
            "inverse_aic",
            "stacking",
            "horizon_adaptive",
        ] {
            assert!(
                parse_combination_method(Some(m)).is_ok(),
                "expected Ok for method: {}",
                m
            );
        }
    }

    #[test]
    fn parse_combination_method_accepts_aliases() {
        // At least one alias per method that has aliases
        assert!(parse_combination_method(Some("weightedmse")).is_ok());
        assert!(parse_combination_method(Some("weighted-mse")).is_ok());
        assert!(parse_combination_method(Some("inverseaic")).is_ok());
        assert!(parse_combination_method(Some("aic")).is_ok());
        assert!(parse_combination_method(Some("stack")).is_ok());
        assert!(parse_combination_method(Some("adaptive")).is_ok());
        assert!(parse_combination_method(Some("horizon-adaptive")).is_ok());
    }

    #[test]
    fn parse_combination_method_none_and_empty_give_mean() {
        // None and empty string are both valid defaults (Mean)
        let none_result = parse_combination_method(None);
        let empty_result = parse_combination_method(Some(""));
        assert!(none_result.is_ok(), "None should parse as Mean");
        assert!(empty_result.is_ok(), "empty string should parse as Mean");
    }

    #[test]
    fn forecast_auto_ensemble_basic() {
        // 60-point linear series; AutoEnsemble with top_k=0 (→ default 3)
        let values: Vec<Option<f64>> = (0..60).map(|i| Some(10.0 + i as f64 * 0.5)).collect();
        let result = forecast(
            &values,
            &ForecastOptions {
                model: ModelType::AutoEnsemble,
                horizon: 5,
                ensemble_top_k: 0, // triggers the "== 0 → 3" default path
                ..Default::default()
            },
        );
        assert!(
            result.is_ok(),
            "AutoEnsemble should succeed on a clean series"
        );
        assert_eq!(
            result.unwrap().point.len(),
            5,
            "output length must equal horizon"
        );
    }

    // -------------------------------------------------------------------------
    // Phase 5 (ENS-02): build_forecaster + forecast_explicit_ensemble (IN-01)
    // -------------------------------------------------------------------------

    #[test]
    fn build_forecaster_ok_for_supported_members() {
        // Naive, AutoETS, and Theta should all return Ok (non-seasonal)
        for model in &[ModelType::Naive, ModelType::AutoETS, ModelType::Theta] {
            let result = build_forecaster(*model, None, 40);
            assert!(
                result.is_ok(),
                "build_forecaster({:?}, None, 40) should succeed",
                model
            );
        }
    }

    #[test]
    fn build_forecaster_err_for_blocked_variants() {
        // GARCH, ARIMA, and AutoEnsemble are blocked
        for model in &[ModelType::GARCH, ModelType::ARIMA, ModelType::AutoEnsemble] {
            let result = build_forecaster(*model, None, 40);
            assert!(
                result.is_err(),
                "build_forecaster({:?}, None, 40) should return Err",
                model
            );
            // Must be an InvalidParameter error with param == "members"
            match result {
                Err(ForecastError::InvalidParameter { param, .. }) => {
                    assert_eq!(param, "members", "error param must be 'members'");
                }
                Err(e) => panic!("expected InvalidParameter, got {:?}", e),
                Ok(_) => panic!("expected Err, got Ok"),
            }
        }
    }

    #[test]
    fn build_forecaster_seasonal_naive_with_period() {
        // SeasonalNaive with period=7 should succeed
        let result = build_forecaster(ModelType::SeasonalNaive, Some(7), 40);
        assert!(
            result.is_ok(),
            "build_forecaster(SeasonalNaive, Some(7), 40) should succeed"
        );
    }

    #[test]
    fn build_forecaster_seasonal_naive_without_period_errors() {
        // D-07: a seasonal-required member with no usable period is a hard
        // error naming the member and seasonal_period — never a silent
        // default of 12.
        let result = build_forecaster(ModelType::SeasonalNaive, None, 40);
        assert!(
            result.is_err(),
            "build_forecaster(SeasonalNaive, None, 40) should return Err"
        );
        match result {
            Err(ForecastError::InvalidParameter { param, .. }) => {
                assert_eq!(param, "seasonal_period");
            }
            Err(e) => panic!("expected InvalidParameter, got {:?}", e),
            Ok(_) => panic!("expected Err, got Ok"),
        }
    }

    #[test]
    fn forecast_explicit_ensemble_basic() {
        // 48-point linear series; ['AutoETS', 'Naive'] with Mean → horizon finite values
        let values: Vec<Option<f64>> = (0..48).map(|i| Some(100.0 + i as f64 * 2.0)).collect();
        let members = vec!["AutoETS".to_string(), "Naive".to_string()];
        let result = forecast_explicit_ensemble(&values, 6, &members, Some("mean"), 0, 0.90);
        assert!(
            result.is_ok(),
            "explicit ensemble on clean series should succeed: {:?}",
            result.err()
        );
        let out = result.unwrap();
        assert_eq!(out.point.len(), 6, "output length must equal horizon");
        for &v in &out.point {
            assert!(
                v.is_finite(),
                "all point forecasts must be finite, got {}",
                v
            );
        }
    }

    #[test]
    fn forecast_explicit_ensemble_rejects_all_null_input() {
        // All-NULL series: fill_nulls_interpolate returns all-NaN (preserves length),
        // so is_empty() is false but Ensemble::fit detects missing values → ComputationError.
        // This mirrors the main forecast() path behaviour for the all-None case.
        let values: Vec<Option<f64>> = vec![None; 10];
        let members = vec!["AutoETS".to_string(), "Naive".to_string()];
        let result = forecast_explicit_ensemble(&values, 3, &members, None, 0, 0.90);
        assert!(result.is_err(), "all-NULL input must return Err");
        // Accept either InsufficientData (empty after interpolation) or ComputationError
        // (NaN detected by ensemble fit); both surface as NULL rows via the FFI.
        match result {
            Err(ForecastError::InsufficientData { .. })
            | Err(ForecastError::ComputationError(_)) => {}
            Err(e) => panic!("expected InsufficientData or ComputationError, got {:?}", e),
            Ok(_) => panic!("expected Err, got Ok"),
        }
    }

    #[test]
    fn forecast_explicit_ensemble_rejects_too_short_series() {
        // Series with only 2 observations must return InsufficientData (< 3 guard)
        let values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let members = vec!["AutoETS".to_string(), "Naive".to_string()];
        let result = forecast_explicit_ensemble(&values, 3, &members, None, 0, 0.90);
        assert!(result.is_err(), "2-observation series must return Err");
        match result.unwrap_err() {
            ForecastError::InsufficientData { needed, got } => {
                assert_eq!(needed, 3);
                assert_eq!(got, 2);
            }
            e => panic!(
                "expected InsufficientData {{ needed: 3, got: 2 }}, got {:?}",
                e
            ),
        }
    }

    #[test]
    fn forecast_explicit_ensemble_duplicate_members_ok() {
        // Duplicate member names should not cause an error
        let values: Vec<Option<f64>> = (0..40).map(|i| Some(i as f64)).collect();
        let members = vec!["AutoETS".to_string(), "AutoETS".to_string()];
        let result = forecast_explicit_ensemble(&values, 4, &members, None, 0, 0.90);
        assert!(
            result.is_ok(),
            "duplicate members should be allowed: {:?}",
            result.err()
        );
    }

    /// FCST-07: for every ModelType build_forecaster can construct, fitting
    /// it directly (the ensemble-member path) and calling
    /// predict_with_intervals(12, 0.95) gives a point forecast bit-identical
    /// to standalone forecast() with the same seasonal_period/confidence —
    /// proving build_forecaster's shared constructors (new_auto_ets,
    /// new_holt_winters, new_seasonal_naive, ...) are not merely similar to
    /// the standalone path but call the exact same crate constructor.
    #[test]
    fn build_forecaster_matches_standalone_forecast_for_every_member() {
        let fx = crate::test_fixtures::load_fixture("r_forecast/air_passengers.json");
        let values: Vec<Option<f64>> = crate::test_fixtures::f64_vec(&fx["inputs"]["values"])
            .into_iter()
            .map(Some)
            .collect();
        let n_obs = values.len();

        // Seasonal-required members (D-07) need an explicit period > 1.
        let seasonal_models: &[ModelType] = &[
            ModelType::HoltWinters,
            ModelType::SeasonalNaive,
            ModelType::SeasonalES,
            ModelType::SeasonalESOptimized,
            ModelType::SeasonalWindowAverage,
        ];
        // Every other ModelType build_forecaster actually constructs
        // (the rest are intentionally unsupported — GARCH, Laplace, ARIMA,
        // MFLES/AutoMFLES, MSTL/AutoMSTL, TBATS/AutoTBATS, AutoEnsemble).
        let non_seasonal_models: &[ModelType] = &[
            ModelType::AutoARIMA,
            ModelType::AutoETS,
            ModelType::AutoTheta,
            ModelType::Theta,
            ModelType::OptimizedTheta,
            ModelType::DynamicTheta,
            ModelType::DynamicOptimizedTheta,
            ModelType::Naive,
            ModelType::RandomWalkDrift,
            ModelType::SES,
            ModelType::SESOptimized,
            ModelType::SMA,
            ModelType::Holt,
            ModelType::ETS,
            ModelType::Kalman,
            ModelType::CrostonClassic,
            ModelType::CrostonOptimized,
            ModelType::CrostonSBA,
            ModelType::ADIDA,
            ModelType::IMAPA,
            ModelType::TSB,
        ];

        let prep = prepare_series(&values).expect("AirPassengers should prepare cleanly");
        assert_eq!(
            prep.trail_trim, 0,
            "test assumes no trailing NULLs so point[] alignment is 1:1"
        );
        let ts = make_timeseries(&prep.values).expect("make_timeseries should succeed");

        for &mt in seasonal_models.iter().chain(non_seasonal_models.iter()) {
            let seasonal = seasonal_models.contains(&mt);
            let period = if seasonal { Some(12) } else { None };

            let mut member = build_forecaster(mt, period, n_obs)
                .unwrap_or_else(|e| panic!("build_forecaster({:?}) failed: {:?}", mt, e));
            member
                .fit(&ts)
                .unwrap_or_else(|e| panic!("{:?} member fit failed: {e}", mt));
            let member_forecast = member
                .predict_with_intervals(12, 0.95)
                .unwrap_or_else(|e| panic!("{:?} member predict failed: {e}", mt));

            let options = ForecastOptions {
                model: mt,
                horizon: 12,
                seasonal_period: period.unwrap_or(0),
                auto_detect_seasonality: false,
                confidence_level: 0.95,
                ..Default::default()
            };
            let standalone = forecast(&values, &options)
                .unwrap_or_else(|e| panic!("standalone forecast({:?}) failed: {:?}", mt, e));

            assert_eq!(
                member_forecast.primary().to_vec(),
                standalone.point,
                "{:?}: ensemble-member point != standalone point",
                mt
            );
        }
    }
}
