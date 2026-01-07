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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_code_conversion() {
        // Each error variant should map to a unique code
        assert_eq!(ForecastError::NullPointer("test".into()).to_code(), 1);
        assert_eq!(ForecastError::InvalidInput("test".into()).to_code(), 2);
        assert_eq!(ForecastError::ComputationError("test".into()).to_code(), 3);
        assert_eq!(ForecastError::AllocationError("test".into()).to_code(), 4);
        assert_eq!(ForecastError::InvalidModel("test".into()).to_code(), 5);
        assert_eq!(
            ForecastError::InsufficientData { needed: 10, got: 5 }.to_code(),
            6
        );
        assert_eq!(ForecastError::InvalidDateFormat("test".into()).to_code(), 7);
        assert_eq!(ForecastError::InvalidFrequency("test".into()).to_code(), 8);
        assert_eq!(
            ForecastError::InvalidParameter {
                param: "alpha".into(),
                value: "2.0".into(),
                reason: "must be between 0 and 1".into()
            }
            .to_code(),
            9
        );
        assert_eq!(ForecastError::InternalError("test".into()).to_code(), 10);
    }

    #[test]
    fn test_error_display() {
        // Test that Display trait produces expected messages
        let err = ForecastError::NullPointer("values array".into());
        assert_eq!(format!("{}", err), "Null pointer argument: values array");

        let err = ForecastError::InvalidInput("negative value not allowed".into());
        assert_eq!(
            format!("{}", err),
            "Invalid input: negative value not allowed"
        );

        let err = ForecastError::InsufficientData { needed: 10, got: 3 };
        assert_eq!(
            format!("{}", err),
            "Insufficient data: need at least 10 observations, got 3"
        );

        let err = ForecastError::InvalidParameter {
            param: "alpha".into(),
            value: "1.5".into(),
            reason: "must be between 0 and 1".into(),
        };
        assert_eq!(
            format!("{}", err),
            "Invalid parameter 'alpha' = '1.5': must be between 0 and 1"
        );

        let err = ForecastError::InvalidModel("UnknownModel".into());
        assert_eq!(format!("{}", err), "Invalid model: UnknownModel");
    }

    #[test]
    fn test_error_construction() {
        // Test that errors can be constructed with various inputs
        let err = ForecastError::NullPointer(String::from("test"));
        assert!(matches!(err, ForecastError::NullPointer(_)));

        let err = ForecastError::InsufficientData { needed: 5, got: 2 };
        if let ForecastError::InsufficientData { needed, got } = err {
            assert_eq!(needed, 5);
            assert_eq!(got, 2);
        } else {
            panic!("Expected InsufficientData variant");
        }

        let err = ForecastError::InvalidParameter {
            param: "window_size".into(),
            value: "-1".into(),
            reason: "must be positive".into(),
        };
        if let ForecastError::InvalidParameter {
            param,
            value,
            reason,
        } = err
        {
            assert_eq!(param, "window_size");
            assert_eq!(value, "-1");
            assert_eq!(reason, "must be positive");
        } else {
            panic!("Expected InvalidParameter variant");
        }
    }
}
