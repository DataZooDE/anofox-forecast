SELECT 
    forecast_step,
    point_forecast,
    lower,              -- Lower bound (5th percentile for 90% CI)
    upper,              -- Upper bound (95th percentile for 90% CI)
    confidence_level    -- Shows CI level used (0.90)
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                 {'seasonal_period': 7, 'confidence_level': 0.90});
