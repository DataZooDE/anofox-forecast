-- Evaluate a 5-quantile forecast distribution
WITH distributions AS (
    SELECT
        [100.0, 110.0, 120.0, 130.0, 140.0] AS actual,
        [
            [90.0, 100.0, 110.0, 120.0, 130.0],    -- q=0.1
            [95.0, 105.0, 115.0, 125.0, 135.0],    -- q=0.25
            [100.0, 110.0, 120.0, 130.0, 140.0],   -- q=0.5 (median)
            [105.0, 115.0, 125.0, 135.0, 145.0],   -- q=0.75
            [110.0, 120.0, 130.0, 140.0, 150.0]    -- q=0.9
        ] AS predicted_quantiles,
        [0.1, 0.25, 0.5, 0.75, 0.9] AS quantiles
)
SELECT 
    TS_MQLOSS(actual, predicted_quantiles, quantiles) AS mqloss,
    'Lower is better - measures full distribution accuracy' AS interpretation
FROM distributions;

-- Result: mqloss = 0.9 (good distribution fit)
