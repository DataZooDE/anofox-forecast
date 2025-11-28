-- Create sample sales data
CREATE TABLE sales_complete AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d);

SELECT * FROM TS_FORECAST(
    'sales_complete',
    date,
    sales_amount,
    'AutoETS',  -- Automatic model selection
    28,         -- 28 days ahead
    MAP{'seasonal_period': 7}
);
