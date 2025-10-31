-- Use many quantiles for better CRPS approximation
WITH dense_quantiles AS (
    SELECT
        actual,
        [q01, q02, q03, ... q99] AS predicted_quantiles,  -- 99 quantiles
        [0.01, 0.02, 0.03, ... 0.99] AS quantiles
    FROM forecast_distributions
)
SELECT 
    model_name,
    TS_MQLOSS(actual, predicted_quantiles, quantiles) AS crps_approximation
FROM dense_quantiles
GROUP BY model_name
ORDER BY crps_approximation;  -- Best model first
