-- Create sample sales data with gaps
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id)
WHERE d % 2 = 0;  -- Create gaps by only including even days

CREATE TABLE sales_filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales', product_id, date, sales_amount, '1d');

-- Before: [2023-01-01, 2023-01-03, 2023-01-05]
-- After:  [2023-01-01, 2023-01-02, 2023-01-03, 2023-01-04, 2023-01-05]
--         values:      [100, NULL, 150, NULL, 200]
