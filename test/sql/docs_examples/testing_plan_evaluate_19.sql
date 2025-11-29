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
    anofox_fcst_ts_mae(actual, forecast) AS mae,
    anofox_fcst_ts_rmse(actual, forecast) AS rmse,
    anofox_fcst_ts_mape(actual, forecast) AS mape,
    anofox_fcst_ts_smape(actual, forecast) AS smape,
    anofox_fcst_ts_mase(actual, forecast, seasonal_period) AS mase,
    anofox_fcst_ts_coverage(actual, lower, upper) AS coverage
FROM forecast_validation;
