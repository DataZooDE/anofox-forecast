-- Create sample forecast validation data
CREATE TABLE forecast_validation AS
SELECT 
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual,
    [101.0, 103.0, 99.0, 106.0]::DOUBLE[] AS forecast,
    [95.0, 97.0, 93.0, 100.0]::DOUBLE[] AS lower,
    [105.0, 107.0, 103.0, 110.0]::DOUBLE[] AS upper,
    [100.0, 100.0, 100.0, 100.0]::DOUBLE[] AS seasonal_period;

-- Test all 12 metrics work
SELECT 
    TS_MAE(actual, forecast) AS mae,
    TS_RMSE(actual, forecast) AS rmse,
    TS_MAPE(actual, forecast) AS mape,
    TS_SMAPE(actual, forecast) AS smape,
    TS_MASE(actual, forecast, seasonal_period) AS mase,
    TS_COVERAGE(actual, lower, upper) AS coverage
FROM forecast_validation;
