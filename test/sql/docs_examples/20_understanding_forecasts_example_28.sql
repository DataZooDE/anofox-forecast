-- Multi-quantile forecasts (not yet implemented)
-- Future feature: Predict distribution, not just mean

-- Workaround: Use different confidence levels
WITH q10 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 0.20, ...})),
q50 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 1.00, ...})),  -- Point forecast
q90 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 0.80, ...}))
SELECT 
    q10.lower AS p10,
    q50.point_forecast AS p50,
    q90.upper AS p90
FROM q10, q50, q90;
