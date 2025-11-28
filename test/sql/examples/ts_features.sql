-- ts_features example: compute feature vector per product
LOAD anofox_forecast;

CREATE OR REPLACE TABLE demand_series AS
SELECT 
    product_id,
    (TIMESTAMP '2024-01-01' + INTERVAL (d) DAY) AS ts,
    (100 + product_id * 10 + d)::DOUBLE AS demand
FROM generate_series(0, 6) t(d)
CROSS JOIN (SELECT 1 AS product_id UNION ALL SELECT 2) p;

SELECT column_name, feature_name, default_parameters, parameter_keys
FROM ts_features_list()
ORDER BY column_name
LIMIT 5;

WITH feature_vec AS (
    SELECT 
        product_id,
        ts_features(
            ts,
            demand,
            ['mean', 'variance', 'autocorrelation__lag_1', 'ratio_beyond_r_sigma'],
            [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
        ) AS feats
    FROM demand_series
    GROUP BY product_id
)
SELECT 
    product_id,
    (feats).mean AS avg_demand,
    (feats).variance AS demand_variance,
    (feats).autocorrelation__lag_1 AS lag1_autocorr,
    (feats).ratio_beyond_r_sigma__r_1 AS outlier_share
FROM feature_vec
ORDER BY product_id;

-- Load overrides from a JSON file and pass the config struct directly
SELECT 
    product_id,
    (ts_features(
        ts,
        demand,
        ts_features_config_from_json('benchmark/timeseries_features/data/features_overrides.json')
    )).autocorrelation__lag_1 AS lag1_autocorr
FROM demand_series
GROUP BY product_id
ORDER BY product_id;

