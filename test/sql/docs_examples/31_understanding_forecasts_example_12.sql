-- Show how uncertainty grows with horizon
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(upper - lower, 2) AS interval_width,
    ROUND(100 * (upper - lower) / point_forecast, 1) AS relative_uncertainty_pct
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 60, {'seasonal_period': 7})
WHERE forecast_step IN (1, 7, 14, 30, 60);

-- Pattern: relative_uncertainty_pct grows with horizon
