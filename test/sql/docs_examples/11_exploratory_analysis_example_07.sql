-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Fill gaps first
CREATE TEMP TABLE filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Remove edge zeros
CREATE TEMP TABLE no_edges AS
SELECT * FROM TS_DROP_EDGE_ZEROS('filled', product_id, date, sales_amount);

-- Fill nulls with interpolation (more sophisticated)
CREATE TABLE sales_custom_prep AS
SELECT 
    product_id,
    date,
    -- Linear interpolation
    COALESCE(sales_amount,
        AVG(sales_amount) OVER (
            PARTITION BY product_id 
            ORDER BY date 
            ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING
        )
    ) AS sales_amount
FROM no_edges;
