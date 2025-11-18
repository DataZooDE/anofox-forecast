-- Extract features for machine learning pipeline
WITH feature_vectors AS (
    SELECT 
        product_id,
        ts_features(
            ts,
            value,
            ['mean', 'variance', 'autocorrelation__lag_1', 'linear_trend', 'sum_values']
        ) AS feats
    FROM sales_data
    GROUP BY product_id
)
SELECT 
    product_id,
    (feats).mean AS avg_value,
    (feats).variance AS variance,
    (feats).autocorrelation__lag_1 AS lag1_autocorr,
    (feats).linear_trend__attr_slope AS trend_slope,
    (feats).sum_values AS total_value
FROM feature_vectors
ORDER BY product_id;

