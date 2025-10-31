-- Measure current performance
.timer on
SELECT * FROM TS_FORECAST_BY(...);
-- Note: 15.2 seconds

-- Try optimization
-- Re-measure to confirm improvement
