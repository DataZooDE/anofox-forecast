//! Error types for the forecast extension.

use thiserror::Error;

/// Result type for forecast operations.
pub type Result<T> = std::result::Result<T, ForecastError>;

/// Error types for forecast extension operations.
#[derive(Error, Debug)]
pub enum ForecastError {
    #[error("Null pointer argument: {0}")]
    NullPointer(String),

    #[error("Invalid input: {0}")]
    InvalidInput(String),

    #[error("Computation error: {0}")]
    ComputationError(String),

    #[error("Allocation error: {0}")]
    AllocationError(String),

    #[error("Invalid model: {0}")]
    InvalidModel(String),

    #[error("Insufficient data: need at least {needed} observations, got {got}")]
    InsufficientData { needed: usize, got: usize },

    #[error("Invalid date format: {0}")]
    InvalidDateFormat(String),

    #[error("Invalid frequency: {0}")]
    InvalidFrequency(String),

    #[error("Invalid parameter '{param}' = '{value}': {reason}")]
    InvalidParameter {
        param: String,
        value: String,
        reason: String,
    },

    #[error("Internal error: {0}")]
    InternalError(String),
}

impl ForecastError {
    /// Convert to an error code for FFI.
    pub fn to_code(&self) -> i32 {
        match self {
            ForecastError::NullPointer(_) => 1,
            ForecastError::InvalidInput(_) => 2,
            ForecastError::ComputationError(_) => 3,
            ForecastError::AllocationError(_) => 4,
            ForecastError::InvalidModel(_) => 5,
            ForecastError::InsufficientData { .. } => 6,
            ForecastError::InvalidDateFormat(_) => 7,
            ForecastError::InvalidFrequency(_) => 8,
            ForecastError::InvalidParameter { .. } => 9,
            ForecastError::InternalError(_) => 10,
        }
    }
}
