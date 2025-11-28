-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) + 
    CASE WHEN RANDOM() < 0.05 THEN 200 ELSE 0 END AS sales_amount  -- Some outliers
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Detect outliers using IQR method
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
),
outliers AS (
    SELECT 
        s.product_id,
        s.date,
        s.sales_amount,
        CASE 
            WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN 'Upper outlier'
            WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN 'Lower outlier'
            ELSE 'Normal'
        END AS outlier_type
    FROM sales s
    JOIN series_bounds b ON s.product_id = b.product_id
)
SELECT product_id, COUNT(*) AS n_outliers
FROM outliers
WHERE outlier_type != 'Normal'
GROUP BY product_id
HAVING COUNT(*) > 0;

-- Fix: Cap outliers (keep them but reduce impact)
-- (Would use TS_CAP_OUTLIERS_IQR if it was in integrated macros)
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        (QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25)) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
)
SELECT 
    s.product_id,
    s.date,
    CASE 
        WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN b.q3 + 1.5 * b.iqr
        WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN b.q1 - 1.5 * b.iqr
        ELSE s.sales_amount
    END AS sales_amount
FROM sales s
JOIN series_bounds b ON s.product_id = b.product_id;
