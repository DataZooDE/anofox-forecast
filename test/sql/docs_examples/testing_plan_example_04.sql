-- Create sample forecast results
CREATE TABLE forecast_results AS
SELECT 
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual_values,
    [95.0, 97.0, 93.0, 100.0]::DOUBLE[] AS lower_bounds,
    [105.0, 107.0, 103.0, 110.0]::DOUBLE[] AS upper_bounds;

-- Generate forecasts with known distribution
-- Check coverage using anofox_fcst_ts_coverage metric
SELECT anofox_fcst_ts_coverage(
    actual_values,
    lower_bounds,
    upper_bounds
) AS coverage
FROM forecast_results;
-- Expected: ~0.90 for 90% confidence level
