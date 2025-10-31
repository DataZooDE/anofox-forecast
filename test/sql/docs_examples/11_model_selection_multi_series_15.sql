-- Recommended: AutoETS or AutoARIMA
SELECT * FROM TS_FORECAST('monthly_revenue', month, revenue,
                          'AutoETS', 12,  -- 1 year ahead
                          {'seasonal_period': 12});

-- Compare with Theta
SELECT * FROM TS_FORECAST('monthly_revenue', month, revenue,
                          'OptimizedTheta', 12,
                          {'seasonal_period': 12});
