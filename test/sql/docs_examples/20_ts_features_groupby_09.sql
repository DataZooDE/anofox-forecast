-- Compute features per group
CREATE TABLE multi_series AS
SELECT 
    (i % 3) AS product_id,
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    (100 + product_id * 10 + i)::DOUBLE AS value
FROM generate_series(0, 20) t(i);

SELECT 
    product_id,
    ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
FROM multi_series
GROUP BY product_id
ORDER BY product_id;

