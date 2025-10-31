{
    'seasonal_period': 7,
    'error_type': 0 | 1,      -- 0=additive, 1=multiplicative
    'trend_type': 0 | 1 | 2,  -- 0=none, 1=additive, 2=damped
    'season_type': 0 | 1 | 2, -- 0=none, 1=additive, 2=multiplicative
    'alpha': 0.0-1.0,         -- Level smoothing (optional, auto-optimized)
    'beta': 0.0-1.0,          -- Trend smoothing (optional)
    'gamma': 0.0-1.0,         -- Seasonal smoothing (optional)
    'phi': 0.0-1.0            -- Damping (optional)
}
