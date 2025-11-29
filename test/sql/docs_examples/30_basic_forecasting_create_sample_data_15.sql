-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Don't guess - detect!
SELECT 
    product_id,
    anofox_fcst_ts_detect_seasonality(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales
GROUP BY product_id;

-- Use detected period in forecast
WITH seasonality AS (
    SELECT 
        product_id, 
        CASE 
            WHEN LEN(anofox_fcst_ts_detect_seasonality(LIST(sales_amount ORDER BY date))) > 0 
            THEN anofox_fcst_ts_detect_seasonality(LIST(sales_amount ORDER BY date))[1]
            ELSE NULL 
        END AS primary_period
    FROM sales
    GROUP BY product_id
)
SELECT f.*
FROM anofox_fcst_ts_forecast_by('sales', product_id, date, sales_amount, 'AutoETS', 28,
                    MAP{'seasonal_period': 7}) f;
