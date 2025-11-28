-- Create sample sales and stats
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Define your own quality criteria
WITH custom_quality AS (
    SELECT 
        series_id,
        -- Custom quality score based on available metrics
        CASE 
            WHEN n_zeros::DOUBLE / NULLIF(length, 0) > 0.5 THEN 0.5  -- Penalize high zeros
            ELSE 1.0
        END AS quality_score,
        -- Custom: Penalize high zero ratio
        CASE 
            WHEN n_zeros::DOUBLE / NULLIF(length, 0) > 0.5 THEN 0.5 * 0.7
            ELSE 1.0
        END AS adjusted_quality,
        -- Custom: Require minimum length
        CASE 
            WHEN length < 60 THEN 0.0
            ELSE 1.0
        END AS length_adjusted_quality
    FROM sales_stats
)
SELECT 
    series_id,
    ROUND(1.0, 4) AS original_quality,
    ROUND(adjusted_quality, 4) AS zero_penalty_adjusted,
    ROUND(length_adjusted_quality, 4) AS length_adjusted
FROM custom_quality
ORDER BY original_quality DESC;
