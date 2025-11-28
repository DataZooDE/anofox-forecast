-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Detect seasonality for trend analysis
WITH seasonality AS (
    SELECT TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
    FROM sales
    WHERE product_id = 1
)
-- Use detected periods for forecasting with seasonal models
SELECT 
    'Use seasonal models like ETS or AutoETS' AS recommendation,
    detected_periods
FROM seasonality;
