-- Create sample multi-product sales data
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

SELECT * FROM TS_FORECAST_BY(
    'sales_complete',
    product_id,     -- Parallel forecasting per product
    date,
    sales_amount,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7, 'confidence_level': 0.95}
);
