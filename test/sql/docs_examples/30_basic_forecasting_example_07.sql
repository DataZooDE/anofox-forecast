-- Create sample forecast results
CREATE TABLE results AS
SELECT 
    1 AS forecast_step,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
UNION ALL
SELECT 2, 105.0, 104.0, 96.0, 112.0
UNION ALL
SELECT 3, 103.0, 105.5, 97.0, 114.0
UNION ALL
SELECT 4, 108.0, 107.0, 98.0, 116.0
UNION ALL
SELECT 5, 106.0, 108.5, 99.0, 118.0;

-- Check if 95% intervals actually cover 95% of actuals
SELECT 
    ROUND(TS_COVERAGE(LIST(actual ORDER BY forecast_step), LIST(lower ORDER BY forecast_step), LIST(upper ORDER BY forecast_step)) * 100, 1) AS coverage_pct
FROM results;

-- Target: ~95% for well-calibrated 95% CI
