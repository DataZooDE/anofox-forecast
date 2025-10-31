-- Evaluate 10th, 50th (median), and 90th percentile forecasts
SELECT 
    TS_QUANTILE_LOSS(actual, lower_bound, 0.1) AS ql_lower,
    TS_QUANTILE_LOSS(actual, median_forecast, 0.5) AS ql_median,
    TS_QUANTILE_LOSS(actual, upper_bound, 0.9) AS ql_upper
FROM forecasts;

-- Perfect median forecast (ql_median = 0.0) means predictions exactly match actuals
-- Lower ql_lower means better lower bound prediction
-- Lower ql_upper means better upper bound prediction
