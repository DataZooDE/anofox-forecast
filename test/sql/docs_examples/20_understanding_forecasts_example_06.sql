-- Analyze uncertainty over horizon
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(upper - lower, 2) AS interval_width,
    ROUND(100 * (upper - lower) / point_forecast, 1) AS width_pct
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
ORDER BY forecast_step;

-- Pattern: Interval width grows with horizon
