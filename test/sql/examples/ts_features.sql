-- ts_features example: compute feature vector per product
LOAD anofox_forecast;

CREATE OR REPLACE TABLE demand_series AS
SELECT 
    product_id,
    (TIMESTAMP '2024-01-01' + INTERVAL day DAY) AS ts,
    (100 + product_id * 10 + day)::DOUBLE AS demand
FROM generate_series(0, 6) t(day)
CROSS JOIN (SELECT 1 AS product_id UNION ALL SELECT 2) p;

SELECT 
    product_id,
    (ts_features(ts, demand)).mean AS avg_demand,
    (ts_features(ts, demand)).variance AS demand_variance,
    (ts_features(ts, demand)).autocorrelation__lag_1 AS lag1_autocorr
FROM demand_series
GROUP BY product_id
ORDER BY product_id;

