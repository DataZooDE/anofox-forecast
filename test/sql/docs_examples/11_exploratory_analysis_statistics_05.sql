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

-- Seasonality
SELECT 
    product_id,
    anofox_fcst_ts_detect_seasonality(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_raw
GROUP BY product_id;

-- Changepoints (regime changes)
SELECT * FROM anofox_fcst_ts_detect_changepoints_by('sales_raw', product_id, date, sales_amount,
                                         MAP{'include_probabilities': true});
