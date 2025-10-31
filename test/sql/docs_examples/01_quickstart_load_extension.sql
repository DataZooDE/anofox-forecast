-- Create a simple daily sales dataset
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Verify data
SELECT * FROM my_sales LIMIT 5;
