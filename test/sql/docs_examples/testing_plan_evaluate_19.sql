-- Test all 12 metrics work
SELECT 
    TS_MAE(actual, forecast) AS mae,
    TS_RMSE(actual, forecast) AS rmse,
    TS_MAPE(actual, forecast) AS mape,
    TS_SMAPE(actual, forecast) AS smape,
    TS_MASE(actual, forecast, seasonal_period) AS mase,
    TS_COVERAGE(actual, lower, upper) AS coverage
FROM forecast_validation;
