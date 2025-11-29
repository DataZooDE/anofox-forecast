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

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- View summary
SELECT * FROM anofox_fcst_ts_stats_summary('sales_stats');

-- Quality report
SELECT * FROM anofox_fcst_ts_quality_report('sales_stats', 30);
