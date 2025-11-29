-- Extract features for machine learning pipeline
-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    'Product_' || LPAD((i % 3 + 1)::VARCHAR, 2, '0') AS product_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS ts,
    GREATEST(0, 
        100.0 + (i % 3 + 1) * 20.0
        + 0.5 * d
        + 15.0 * SIN(2 * PI() * d / 7)
        + (RANDOM() * 10.0 - 5.0)
    )::DOUBLE AS value
FROM generate_series(0, 89) t(d)
CROSS JOIN generate_series(1, 3) t(i);

-- Extract features with explicit selection
SELECT 
    product_id,
    feats.*
FROM (
    SELECT 
        product_id,
        anofox_fcst_ts_features(
            ts,
            value,
            ['mean', 'variance', 'autocorrelation__lag_1', 'linear_trend', 'sum_values']
        ) AS feats
    FROM sales_data
    GROUP BY product_id
)
ORDER BY product_id;

