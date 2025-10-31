-- Forecast 90 days: Slower
SELECT * FROM TS_FORECAST(..., 90, ...);

-- Forecast 7 days, re-run weekly: Faster
SELECT * FROM TS_FORECAST(..., 7, ...);
