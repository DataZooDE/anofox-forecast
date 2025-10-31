-- Generate forecasts with known distribution
-- Check coverage using TS_COVERAGE metric
SELECT TS_COVERAGE(
    actual_values,
    lower_bounds,
    upper_bounds
) AS coverage
FROM forecast_results;
-- Expected: ~0.90 for 90% confidence level
