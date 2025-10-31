{
    -- Universal
    'confidence_level': 0.80 | 0.90 | 0.95 | 0.99,  -- Default: 0.90
    'return_insample': true | false,                 -- Default: false
    'seasonal_period': INT,                          -- Required for seasonal models
    'seasonal_periods': [INT, ...],                  -- Multiple seasonality
    
    -- ETS
    'error_type': 0 | 1,      -- 0=additive, 1=multiplicative
    'trend_type': 0 | 1 | 2,  -- 0=none, 1=additive, 2=damped
    'season_type': 0 | 1 | 2, -- 0=none, 1=additive, 2=multiplicative
    'alpha': 0.0-1.0,         -- Level smoothing
    'beta': 0.0-1.0,          -- Trend smoothing
    'gamma': 0.0-1.0,         -- Seasonal smoothing
    'phi': 0.0-1.0,           -- Damping parameter
    
    -- ARIMA
    'p': INT,                 -- AR order
    'd': INT,                 -- Differencing
    'q': INT,                 -- MA order
    'P': INT,                 -- Seasonal AR
    'D': INT,                 -- Seasonal differencing
    'Q': INT,                 -- Seasonal MA
    's': INT,                 -- Seasonal period
    'include_intercept': BOOL,
    
    -- Theta
    'theta': DOUBLE,          -- Theta parameter (default: 2.0)
    
    -- TBATS
    'use_box_cox': BOOL,
    'box_cox_lambda': DOUBLE,
    'use_trend': BOOL,
    'use_damped_trend': BOOL,
    
    -- MFLES/MSTL
    'n_iterations': INT,
    'lr_trend': DOUBLE,
    'lr_season': DOUBLE,
    'lr_level': DOUBLE,
    'trend_method': INT,
    'seasonal_method': INT
}
