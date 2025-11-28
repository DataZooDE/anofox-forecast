-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + (ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY product_id) % 3 + 1) * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create product catalog
CREATE TABLE product_catalog AS
SELECT 
    product_id,
    CASE 
        WHEN product_id = 'P001' THEN 'Electronics'
        WHEN product_id = 'P002' THEN 'Clothing'
        ELSE 'Electronics'
    END AS category
FROM (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Forecast at category level, then disaggregate
WITH category_forecast AS (
    SELECT 
        pc.category,
        date,
        SUM(sales) AS category_sales
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY pc.category, date
),
category_predictions AS (
    SELECT * FROM TS_FORECAST_BY('category_forecast', category, date, category_sales,
                                 'AutoETS', 28, MAP{'seasonal_period': 7})
),
product_proportions AS (
    SELECT 
        s.product_id,
        pc.category,
        AVG(sales) / SUM(AVG(sales)) OVER (PARTITION BY pc.category) AS product_share
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY s.product_id, pc.category
)
SELECT 
    pp.product_id,
    cp.date AS forecast_date,
    ROUND(cp.point_forecast * pp.product_share, 2) AS product_forecast
FROM category_predictions cp
JOIN product_proportions pp ON cp.category = pp.category;
