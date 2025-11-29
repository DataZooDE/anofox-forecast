-- Create sample product sales data
CREATE TABLE product_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Detect changepoints across 1000s of products
SELECT 
    product_id,
    date_col AS date,
    value_col AS amount,
    is_changepoint
FROM anofox_fcst_ts_detect_changepoints_by('product_sales', product_id, date, amount, MAP{})
WHERE is_changepoint = true
ORDER BY product_id, date_col;

-- Find products with recent changes
WITH changes AS (
    SELECT 
        product_id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_change
    FROM anofox_fcst_ts_detect_changepoints_by('product_sales', product_id, date, amount, MAP{})
    GROUP BY product_id
)
SELECT 
    product_id,
    last_change,
    DATE_DIFF('day', last_change, CURRENT_DATE) AS days_since_change
FROM changes
WHERE last_change >= CURRENT_DATE - INTERVAL '30 days'
ORDER BY last_change DESC;
