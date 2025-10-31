SELECT 
    forecast_step,
    point_forecast    -- This is the expected value
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});
