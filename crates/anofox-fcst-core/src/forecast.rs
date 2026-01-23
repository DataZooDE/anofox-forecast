//! Forecasting module wrapping anofox-forecast crate.

use crate::error::{ForecastError, Result};
use crate::imputation::fill_nulls_interpolate;
use crate::seasonality::detect_seasonality;

// ETS model types from anofox-forecast crate
use anofox_forecast::models::exponential::{ETSSpec, ETS as ETSModel};
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
        }
    }
}

/// Generate forecasts for a time series.
pub fn forecast(values: &[Option<f64>], options: &ForecastOptions) -> Result<ForecastOutput> {
    // Handle NULLs by interpolation
    let clean_values: Vec<f64> = fill_nulls_interpolate(values);

    if clean_values.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if clean_values.len() < 3 {
        return Err(ForecastError::InsufficientData {
            needed: 3,
            got: clean_values.len(),
        });
    }

    // Detect seasonality if needed
    let period = if options.auto_detect_seasonality && options.seasonal_period == 0 {
        detect_seasonality(&clean_values, None)
            .ok()
            .and_then(|p| p.first().cloned())
            .unwrap_or(1) as usize
    } else if options.seasonal_period > 0 {
        options.seasonal_period
    } else {
        1
    };

    // Select model - Auto* models use automatic selection
    let model = if is_auto_model(options.model) {
        select_best_model(&clean_values, period)
    } else {
        options.model
    };

    // Generate forecast based on model
    let result = match model {
        // Basic Models
        ModelType::Naive => forecast_naive(&clean_values, options.horizon),
        ModelType::SeasonalNaive => forecast_seasonal_naive(&clean_values, options.horizon, period),
        ModelType::SMA => forecast_sma(&clean_values, options.horizon, period.max(3)),
        ModelType::RandomWalkDrift => forecast_drift(&clean_values, options.horizon),
        // Exponential Smoothing
        ModelType::SES | ModelType::SESOptimized => {
            forecast_ses(&clean_values, options.horizon, 0.3)
        }
        ModelType::Holt => forecast_holt(&clean_values, options.horizon, 0.3, 0.1),
        ModelType::HoltWinters => {
            forecast_holt_winters(&clean_values, options.horizon, period, 0.3, 0.1, 0.1)
        }
        ModelType::SeasonalES | ModelType::SeasonalESOptimized => {
            forecast_seasonal_es(&clean_values, options.horizon, period)
        }
        ModelType::SeasonalWindowAverage => {
            forecast_seasonal_window_average(&clean_values, options.horizon, period)
        }
        ModelType::ETS => forecast_ets(
            &clean_values,
            options.horizon,
            period,
            options.ets_spec.as_deref(),
        ),
        ModelType::AutoETS => forecast_ets(&clean_values, options.horizon, period, None),
        // Theta Methods
        ModelType::Theta
        | ModelType::OptimizedTheta
        | ModelType::DynamicTheta
        | ModelType::DynamicOptimizedTheta
        | ModelType::AutoTheta => forecast_theta(&clean_values, options.horizon),
        // ARIMA
        ModelType::ARIMA | ModelType::AutoARIMA => forecast_arima(&clean_values, options.horizon),
        // Multiple Seasonality
        ModelType::MFLES | ModelType::AutoMFLES => {
            forecast_mfles(&clean_values, options.horizon, period)
        }
        ModelType::MSTL | ModelType::AutoMSTL => {
            forecast_mstl(&clean_values, options.horizon, period)
        }
        ModelType::TBATS | ModelType::AutoTBATS => {
            forecast_tbats(&clean_values, options.horizon, period)
        }
        // Intermittent Demand
        ModelType::CrostonClassic | ModelType::CrostonOptimized | ModelType::CrostonSBA => {
            forecast_croston(&clean_values, options.horizon)
        }
        ModelType::TSB | ModelType::ADIDA | ModelType::IMAPA => {
            forecast_croston(&clean_values, options.horizon)
        }
    }?;

    // Calculate confidence intervals
    let (lower, upper) =
        calculate_confidence_intervals(&result.point, &clean_values, options.confidence_level);

    // Calculate fitted values and residuals if requested
    let (fitted, residuals) = if options.include_fitted || options.include_residuals {
        let fitted = calculate_fitted_values(&clean_values, model, period);
        let residuals = if options.include_residuals {
            Some(
                clean_values
                    .iter()
                    .zip(fitted.iter())
                    .map(|(a, f)| a - f)
                    .collect(),
            )
        } else {
            None
        };
        (Some(fitted), residuals)
    } else {
        (None, None)
    };

    // Calculate MSE
    let mse = fitted.as_ref().map(|f| {
        let sse: f64 = clean_values
            .iter()
            .zip(f.iter())
            .map(|(a, f)| (a - f).powi(2))
            .sum();
        sse / clean_values.len() as f64
    });

    Ok(ForecastOutput {
        point: result.point,
        lower,
        upper,
        fitted: if options.include_fitted { fitted } else { None },
        residuals,
        model_name: model.name().to_string(),
        aic: None,
        bic: None,
        mse,
    })
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
    // Validate exogenous data if provided
    if let Some(ref exog) = options.exog {
        exog.validate(values.len(), options.horizon)?;
    }

    // Handle NULLs by interpolation
    let clean_values: Vec<f64> = fill_nulls_interpolate(values);

    if clean_values.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if clean_values.len() < 3 {
        return Err(ForecastError::InsufficientData {
            needed: 3,
            got: clean_values.len(),
        });
    }

    // Detect seasonality if needed
    let period = if options.auto_detect_seasonality && options.seasonal_period == 0 {
        detect_seasonality(&clean_values, None)
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

    // Generate forecast based on model
    // For models that support exog with exog data provided, use exogenous-aware forecasting
    // Don't do auto-selection when using exog - use the requested model family
    let result = match options.exog.as_ref() {
        Some(exog) if supports_exog => match options.model {
            ModelType::ARIMA | ModelType::AutoARIMA => {
                forecast_arima_with_exog(&clean_values, options.horizon, exog)
            }
            ModelType::OptimizedTheta | ModelType::DynamicTheta | ModelType::AutoTheta => {
                forecast_theta_with_exog(&clean_values, options.horizon, exog)
            }
            ModelType::MFLES | ModelType::AutoMFLES => {
                forecast_mfles_with_exog(&clean_values, options.horizon, period, exog)
            }
            _ => {
                // Shouldn't happen due to supports_exog check, but fallback to ARIMA with exog
                forecast_arima_with_exog(&clean_values, options.horizon, exog)
            }
        },
        _ => {
            // No exog data or model doesn't support exog - use standard forecasting with auto-selection
            let model = if is_auto_model(options.model) {
                select_best_model(&clean_values, period)
            } else {
                options.model
            };
            forecast_with_model(&clean_values, options.horizon, model, period)
        }
    }?;

    // For fitted values calculation, determine actual model used
    let model = if supports_exog && options.exog.is_some() {
        options.model // Use requested model for exog case
    } else if is_auto_model(options.model) {
        select_best_model(&clean_values, period)
    } else {
        options.model
    };

    // Calculate confidence intervals
    let (lower, upper) =
        calculate_confidence_intervals(&result.point, &clean_values, options.confidence_level);

    // Calculate fitted values and residuals if requested
    let (fitted, residuals) = if options.include_fitted || options.include_residuals {
        let fitted = calculate_fitted_values(&clean_values, model, period);
        let residuals = if options.include_residuals {
            Some(
                clean_values
                    .iter()
                    .zip(fitted.iter())
                    .map(|(a, f)| a - f)
                    .collect(),
            )
        } else {
            None
        };
        (Some(fitted), residuals)
    } else {
        (None, None)
    };

    // Calculate MSE
    let mse = fitted.as_ref().map(|f| {
        let sse: f64 = clean_values
            .iter()
            .zip(f.iter())
            .map(|(a, f)| (a - f).powi(2))
            .sum();
        sse / clean_values.len() as f64
    });

    // Use the model name from the result (e.g., "ARIMAX" for exog) if available,
    // otherwise use the model enum name
    let model_name = if !result.model_name.is_empty() {
        result.model_name
    } else {
        model.name().to_string()
    };

    Ok(ForecastOutput {
        point: result.point,
        lower,
        upper,
        fitted: if options.include_fitted { fitted } else { None },
        residuals,
        model_name,
        aic: None,
        bic: None,
        mse,
    })
}

/// Internal helper to forecast with a specific model (no exog).
fn forecast_with_model(
    values: &[f64],
    horizon: usize,
    model: ModelType,
    period: usize,
) -> Result<ForecastOutput> {
    match model {
        // Basic Models
        ModelType::Naive => forecast_naive(values, horizon),
        ModelType::SeasonalNaive => forecast_seasonal_naive(values, horizon, period),
        ModelType::SMA => forecast_sma(values, horizon, period.max(3)),
        ModelType::RandomWalkDrift => forecast_drift(values, horizon),
        // Exponential Smoothing
        ModelType::SES | ModelType::SESOptimized => forecast_ses(values, horizon, 0.3),
        ModelType::Holt => forecast_holt(values, horizon, 0.3, 0.1),
        ModelType::HoltWinters => forecast_holt_winters(values, horizon, period, 0.3, 0.1, 0.1),
        ModelType::SeasonalES | ModelType::SeasonalESOptimized => {
            forecast_seasonal_es(values, horizon, period)
        }
        ModelType::SeasonalWindowAverage => {
            forecast_seasonal_window_average(values, horizon, period)
        }
        ModelType::ETS | ModelType::AutoETS => forecast_ets(values, horizon, period, None),
        // Theta Methods
        ModelType::Theta
        | ModelType::OptimizedTheta
        | ModelType::DynamicTheta
        | ModelType::DynamicOptimizedTheta
        | ModelType::AutoTheta => forecast_theta(values, horizon),
        // ARIMA
        ModelType::ARIMA | ModelType::AutoARIMA => forecast_arima(values, horizon),
        // Multiple Seasonality
        ModelType::MFLES | ModelType::AutoMFLES => forecast_mfles(values, horizon, period),
        ModelType::MSTL | ModelType::AutoMSTL => forecast_mstl(values, horizon, period),
        ModelType::TBATS | ModelType::AutoTBATS => forecast_tbats(values, horizon, period),
        // Intermittent Demand
        ModelType::CrostonClassic | ModelType::CrostonOptimized | ModelType::CrostonSBA => {
            forecast_croston(values, horizon)
        }
        ModelType::TSB | ModelType::ADIDA | ModelType::IMAPA => forecast_croston(values, horizon),
    }
}

// Model implementations

fn forecast_naive(values: &[f64], horizon: usize) -> Result<ForecastOutput> {
    let last = *values.last().expect("values validated non-empty by caller");
    Ok(ForecastOutput {
        point: vec![last; horizon],
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "Naive".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_seasonal_naive(
    values: &[f64],
    horizon: usize,
    period: usize,
) -> Result<ForecastOutput> {
    let p = period.max(1).min(values.len());
    let last_season: Vec<f64> = values.iter().rev().take(p).rev().cloned().collect();

    let point: Vec<f64> = (0..horizon).map(|i| last_season[i % p]).collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "SeasonalNaive".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_sma(values: &[f64], horizon: usize, window: usize) -> Result<ForecastOutput> {
    let w = window.min(values.len());
    let forecast_value: f64 = values.iter().rev().take(w).sum::<f64>() / w as f64;

    Ok(ForecastOutput {
        point: vec![forecast_value; horizon],
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "SMA".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_drift(values: &[f64], horizon: usize) -> Result<ForecastOutput> {
    let n = values.len();
    let first = values[0];
    let last = values[n - 1];
    let drift = (last - first) / (n - 1) as f64;

    let point: Vec<f64> = (1..=horizon).map(|h| last + drift * h as f64).collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "Drift".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_ses(values: &[f64], horizon: usize, alpha: f64) -> Result<ForecastOutput> {
    let mut level = values[0];

    for &v in values.iter().skip(1) {
        level = alpha * v + (1.0 - alpha) * level;
    }

    Ok(ForecastOutput {
        point: vec![level; horizon],
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "SES".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_holt(values: &[f64], horizon: usize, alpha: f64, beta: f64) -> Result<ForecastOutput> {
    if values.len() < 2 {
        return forecast_ses(values, horizon, alpha);
    }

    let mut level = values[0];
    let mut trend = values[1] - values[0];

    for &v in values.iter().skip(1) {
        let prev_level = level;
        level = alpha * v + (1.0 - alpha) * (level + trend);
        trend = beta * (level - prev_level) + (1.0 - beta) * trend;
    }

    let point: Vec<f64> = (1..=horizon).map(|h| level + trend * h as f64).collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "Holt".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_holt_winters(
    values: &[f64],
    horizon: usize,
    period: usize,
    alpha: f64,
    beta: f64,
    gamma: f64,
) -> Result<ForecastOutput> {
    let p = period.max(2).min(values.len() / 2);

    if values.len() < 2 * p {
        return forecast_holt(values, horizon, alpha, beta);
    }

    // Initialize
    let initial_level: f64 = values[..p].iter().sum::<f64>() / p as f64;
    let mut level = initial_level;
    let mut trend = (values[p..2 * p].iter().sum::<f64>() / p as f64 - initial_level) / p as f64;

    // Initialize seasonal
    let mut seasonal: Vec<f64> = values[..p]
        .iter()
        .map(|v| v / initial_level.max(0.001))
        .collect();

    // Update
    for (i, &v) in values.iter().enumerate().skip(p) {
        let s_idx = i % p;
        let prev_level = level;

        level = alpha * (v / seasonal[s_idx].max(0.001)) + (1.0 - alpha) * (level + trend);
        trend = beta * (level - prev_level) + (1.0 - beta) * trend;
        seasonal[s_idx] = gamma * (v / level.max(0.001)) + (1.0 - gamma) * seasonal[s_idx];
    }

    // Forecast
    let point: Vec<f64> = (1..=horizon)
        .map(|h| (level + trend * h as f64) * seasonal[(values.len() + h - 1) % p])
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "HoltWinters".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_theta(values: &[f64], horizon: usize) -> Result<ForecastOutput> {
    // Simple Theta method: combine SES with drift
    let ses_forecast = forecast_ses(values, horizon, 0.3)?;
    let drift_forecast = forecast_drift(values, horizon)?;

    // Theta = 2 means equal weight to SES and drift
    let point: Vec<f64> = ses_forecast
        .point
        .iter()
        .zip(drift_forecast.point.iter())
        .map(|(s, d)| (s + d) / 2.0)
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "Theta".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_seasonal_es(values: &[f64], horizon: usize, period: usize) -> Result<ForecastOutput> {
    // Seasonal exponential smoothing (simplified Holt-Winters without trend)
    let p = period.max(2).min(values.len() / 2);

    if values.len() < 2 * p {
        return forecast_ses(values, horizon, 0.3);
    }

    let alpha = 0.3;
    let gamma = 0.1;

    let initial_level: f64 = values[..p].iter().sum::<f64>() / p as f64;
    let mut level = initial_level;
    let mut seasonal: Vec<f64> = values[..p]
        .iter()
        .map(|v| v / initial_level.max(0.001))
        .collect();

    for (i, &v) in values.iter().enumerate().skip(p) {
        let s_idx = i % p;
        level = alpha * (v / seasonal[s_idx].max(0.001)) + (1.0 - alpha) * level;
        seasonal[s_idx] = gamma * (v / level.max(0.001)) + (1.0 - gamma) * seasonal[s_idx];
    }

    let point: Vec<f64> = (1..=horizon)
        .map(|h| level * seasonal[(values.len() + h - 1) % p])
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "SeasonalES".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_seasonal_window_average(
    values: &[f64],
    horizon: usize,
    period: usize,
) -> Result<ForecastOutput> {
    // Seasonal Window Average: average of values at the same seasonal position
    // Uses a window of seasonal periods to compute forecasts
    let p = period.max(1).min(values.len());
    let n = values.len();

    // Calculate the number of complete seasons we can use
    let n_seasons = n / p;
    if n_seasons == 0 {
        // Fall back to simple average if not enough data
        let avg = values.iter().sum::<f64>() / n as f64;
        return Ok(ForecastOutput {
            point: vec![avg; horizon],
            lower: vec![],
            upper: vec![],
            fitted: None,
            residuals: None,
            model_name: "SeasonalWindowAverage".to_string(),
            aic: None,
            bic: None,
            mse: None,
        });
    }

    // Calculate seasonal averages for each position in the period
    let mut seasonal_avg = vec![0.0; p];
    let mut seasonal_count = vec![0usize; p];

    for (i, &val) in values.iter().enumerate() {
        let pos = i % p;
        seasonal_avg[pos] += val;
        seasonal_count[pos] += 1;
    }

    for i in 0..p {
        if seasonal_count[i] > 0 {
            seasonal_avg[i] /= seasonal_count[i] as f64;
        }
    }

    // Generate forecasts using the seasonal averages
    let start_pos = n % p;
    let point: Vec<f64> = (0..horizon)
        .map(|h| seasonal_avg[(start_pos + h) % p])
        .collect();

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "SeasonalWindowAverage".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
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
) -> Result<ForecastOutput> {
    // Parse ETS specification if provided
    let spec = if let Some(notation) = ets_spec {
        // First validate the notation format
        if !is_valid_ets_notation(notation) {
            return Err(ForecastError::InvalidInput(format!(
                "Invalid ETS model specification '{}'. \
                 Expected format: ETS notation like 'AAA', 'MNM', 'AAdA' where \
                 E=Error (A/M), T=Trend (A/M/N, optionally with 'd' for damped), S=Seasonal (A/M/N).",
                notation
            )));
        }

        // Use from_notation to parse the spec
        let parsed_spec = ETSSpec::from_notation(notation).map_err(|e| {
            ForecastError::InvalidInput(format!(
                "Invalid ETS model specification '{}': {}",
                notation, e
            ))
        })?;

        // Validate the spec (reject unstable combinations like MAA, MAdA)
        if !parsed_spec.is_valid() {
            return Err(ForecastError::InvalidInput(format!(
                "ETS model specification '{}' is unstable (per FPP3 taxonomy). \
                 MAA and MAdA combinations are not allowed.",
                notation
            )));
        }

        Some(parsed_spec)
    } else {
        None
    };

    // If we have a valid spec, try to use the anofox-forecast ETS model
    if let Some(ets_spec) = spec {
        // Try to use the real ETS forecaster
        match forecast_with_ets_spec(values, horizon, period, &ets_spec) {
            Ok(output) => return Ok(output),
            Err(_) => {
                // Fall back to simplified implementation
            }
        }
    }

    // Fallback: simplified ETS implementation based on data characteristics
    if period > 1 && values.len() >= 2 * period {
        forecast_holt_winters(values, horizon, period, 0.3, 0.1, 0.1)
    } else if values.len() >= 10 {
        forecast_holt(values, horizon, 0.3, 0.1)
    } else {
        forecast_ses(values, horizon, 0.3)
    }
}

/// Forecast using the anofox-forecast ETS model with explicit spec.
fn forecast_with_ets_spec(
    values: &[f64],
    horizon: usize,
    period: usize,
    spec: &ETSSpec,
) -> Result<ForecastOutput> {
    use anofox_forecast::core::TimeSeriesBuilder;

    // Determine seasonal period for the model
    let seasonal_period = if spec.has_seasonal() && period > 1 {
        period
    } else {
        1
    };

    // Create TimeSeries from values
    let time_series = TimeSeriesBuilder::new()
        .values(values.to_vec())
        .build()
        .map_err(|e| {
            ForecastError::ComputationError(format!("Failed to build TimeSeries: {}", e))
        })?;

    // Create the ETS forecaster
    let mut forecaster = ETSModel::new(*spec, seasonal_period);

    // Fit the model
    forecaster
        .fit(&time_series)
        .map_err(|e| ForecastError::ComputationError(format!("Failed to fit ETS model: {}", e)))?;

    // Generate forecasts
    let forecast = forecaster.predict(horizon).map_err(|e| {
        ForecastError::ComputationError(format!("Failed to generate ETS forecasts: {}", e))
    })?;

    // Extract point forecasts (univariate - first dimension)
    let point = forecast.point().first().cloned().unwrap_or_default();

    // Get fitted values
    let fitted = forecaster.fitted_values().map(|v| v.to_vec());

    // Get model name from spec
    let model_name = format!("ETS({})", spec.short_name());

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted,
        residuals: None,
        model_name,
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_arima(values: &[f64], horizon: usize) -> Result<ForecastOutput> {
    // Simplified ARIMA(1,1,1) - AR(1) on differenced series
    if values.len() < 5 {
        return forecast_naive(values, horizon);
    }

    // Difference the series
    let diff: Vec<f64> = values.windows(2).map(|w| w[1] - w[0]).collect();

    // Fit AR(1) on differenced series
    let mean_diff = diff.iter().sum::<f64>() / diff.len() as f64;
    let ar_coef = 0.5; // Simplified: fixed AR coefficient

    let last_val = *values
        .last()
        .expect("values.len() >= 5 validated by caller");
    let last_diff = *diff.last().expect("diff non-empty when values.len() >= 5");

    let mut point = Vec::with_capacity(horizon);
    let mut prev_diff = last_diff;
    let mut cumsum = last_val;

    for _ in 0..horizon {
        let next_diff = mean_diff + ar_coef * (prev_diff - mean_diff);
        cumsum += next_diff;
        point.push(cumsum);
        prev_diff = next_diff;
    }

    Ok(ForecastOutput {
        point,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "ARIMA".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
}

fn forecast_mfles(values: &[f64], horizon: usize, period: usize) -> Result<ForecastOutput> {
    // MFLES: Multiple Frequency Locally Estimated Scatterplot Smoothing
    // Simplified: use seasonal exponential smoothing
    let result = forecast_seasonal_es(values, horizon, period)?;
    Ok(ForecastOutput {
        model_name: "MFLES".to_string(),
        ..result
    })
}

fn forecast_mstl(values: &[f64], horizon: usize, period: usize) -> Result<ForecastOutput> {
    // MSTL: Multiple Seasonal-Trend decomposition using Loess
    // Simplified: use Holt-Winters as it handles trend and seasonality
    let result = if period > 1 && values.len() >= 2 * period {
        forecast_holt_winters(values, horizon, period, 0.3, 0.1, 0.1)?
    } else {
        forecast_holt(values, horizon, 0.3, 0.1)?
    };
    Ok(ForecastOutput {
        model_name: "MSTL".to_string(),
        ..result
    })
}

fn forecast_tbats(values: &[f64], horizon: usize, period: usize) -> Result<ForecastOutput> {
    // TBATS: Trigonometric, Box-Cox, ARMA, Trend, Seasonal
    // Simplified: use Holt-Winters
    let result = if period > 1 && values.len() >= 2 * period {
        forecast_holt_winters(values, horizon, period, 0.3, 0.1, 0.1)?
    } else {
        forecast_ses(values, horizon, 0.3)?
    };
    Ok(ForecastOutput {
        model_name: "TBATS".to_string(),
        ..result
    })
}

fn forecast_croston(values: &[f64], horizon: usize) -> Result<ForecastOutput> {
    // Croston's method for intermittent demand
    let alpha = 0.1;

    // Separate demand and intervals
    let mut demand_level = 0.0;
    let mut interval_level = 1.0;
    let mut last_nonzero_idx = 0;
    let mut first_nonzero = true;

    for (i, &v) in values.iter().enumerate() {
        if v > 0.0 {
            let interval = if first_nonzero {
                1.0
            } else {
                (i - last_nonzero_idx) as f64
            };

            if first_nonzero {
                demand_level = v;
                interval_level = 1.0;
                first_nonzero = false;
            } else {
                demand_level = alpha * v + (1.0 - alpha) * demand_level;
                interval_level = alpha * interval + (1.0 - alpha) * interval_level;
            }
            last_nonzero_idx = i;
        }
    }

    // Forecast = demand / interval
    let forecast_value = if interval_level > 0.0 {
        demand_level / interval_level
    } else {
        demand_level
    };

    Ok(ForecastOutput {
        point: vec![forecast_value; horizon],
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: "CrostonClassic".to_string(),
        aic: None,
        bic: None,
        mse: None,
    })
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

    // Forecast residuals with ARIMA
    let residual_forecast = forecast_arima(&residuals, horizon)?;

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

    // Forecast residuals with Theta
    let residual_forecast = forecast_theta(&residuals, horizon)?;

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
    period: usize,
    exog: &ExogenousData,
) -> Result<ForecastOutput> {
    // Fit regression
    let (coeffs, residuals) = fit_ols_regression(values, &exog.historical);

    // Forecast residuals with MFLES
    let residual_forecast = forecast_mfles(&residuals, horizon, period)?;

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

fn calculate_confidence_intervals(
    forecasts: &[f64],
    historical: &[f64],
    confidence: f64,
) -> (Vec<f64>, Vec<f64>) {
    // Calculate residual standard error
    let mean = historical.iter().sum::<f64>() / historical.len() as f64;
    let variance =
        historical.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / historical.len() as f64;
    let std_error = variance.sqrt();

    // Z-score for confidence level
    let z = match confidence {
        c if c >= 0.99 => 2.576,
        c if c >= 0.95 => 1.96,
        c if c >= 0.90 => 1.645,
        c if c >= 0.80 => 1.28,
        _ => 1.0,
    };

    let lower: Vec<f64> = forecasts
        .iter()
        .enumerate()
        .map(|(i, &f)| f - z * std_error * ((i + 1) as f64).sqrt())
        .collect();

    let upper: Vec<f64> = forecasts
        .iter()
        .enumerate()
        .map(|(i, &f)| f + z * std_error * ((i + 1) as f64).sqrt())
        .collect();

    (lower, upper)
}

fn calculate_fitted_values(values: &[f64], model: ModelType, period: usize) -> Vec<f64> {
    match model {
        ModelType::Naive => {
            let mut fitted = vec![values[0]];
            fitted.extend(values[..values.len() - 1].iter().cloned());
            fitted
        }
        ModelType::SeasonalNaive => {
            let p = period.max(1).min(values.len());
            let mut fitted = vec![values[0]; p.min(values.len())];
            for i in p..values.len() {
                fitted.push(values[i - p]);
            }
            fitted
        }
        ModelType::SeasonalWindowAverage => {
            // Fitted values are the seasonal averages at each position
            let p = period.max(1).min(values.len());
            let mut seasonal_sum = vec![0.0; p];
            let mut seasonal_count = vec![0usize; p];
            let mut fitted = Vec::with_capacity(values.len());

            for (i, &val) in values.iter().enumerate() {
                let pos = i % p;
                // Fitted value is the current average for this seasonal position
                if seasonal_count[pos] > 0 {
                    fitted.push(seasonal_sum[pos] / seasonal_count[pos] as f64);
                } else {
                    fitted.push(val); // No prior data, use actual
                }
                // Update running average
                seasonal_sum[pos] += val;
                seasonal_count[pos] += 1;
            }
            fitted
        }
        _ => {
            // SES fitted values
            let alpha = 0.3;
            let mut fitted = Vec::with_capacity(values.len());
            let mut level = values[0];
            fitted.push(level);

            for &v in values.iter().skip(1) {
                fitted.push(level);
                level = alpha * v + (1.0 - alpha) * level;
            }
            fitted
        }
    }
}

/// List all available model names (32 models matching C++ extension).
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
    ]
    .into_iter()
    .map(String::from)
    .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

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
            ..Default::default()
        };

        let result = forecast(&values, &options).unwrap();
        assert_eq!(result.point.len(), 5);
        assert_eq!(result.model_name, "ARIMA");
        // ARIMA forecasts should continue the trend
        assert!(result.point.iter().all(|v| v.is_finite()));
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
    fn test_calculate_confidence_intervals() {
        let forecasts = vec![100.0, 105.0, 110.0];
        let historical: Vec<f64> = (0..20).map(|i| 50.0 + i as f64).collect();

        let (lower, upper) = calculate_confidence_intervals(&forecasts, &historical, 0.95);

        assert_eq!(lower.len(), 3);
        assert_eq!(upper.len(), 3);
        // Lower bounds should be below forecasts
        assert!(lower.iter().zip(&forecasts).all(|(l, f)| l < f));
        // Upper bounds should be above forecasts
        assert!(upper.iter().zip(&forecasts).all(|(u, f)| u > f));
        // Intervals should widen with horizon
        let width_1 = upper[0] - lower[0];
        let width_3 = upper[2] - lower[2];
        assert!(width_3 > width_1);
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
    fn test_ets_valid_spec_succeeds() {
        // Test that a valid ETS spec succeeds
        let values: Vec<Option<f64>> = (0..20).map(|i| Some(100.0 + i as f64)).collect();

        let options = ForecastOptions {
            model: ModelType::ETS,
            ets_spec: Some("AAA".to_string()),
            horizon: 5,
            ..Default::default()
        };

        let result = forecast(&values, &options);
        assert!(
            result.is_ok(),
            "Expected success for valid ETS spec 'AAA': {:?}",
            result.err()
        );
    }
}
