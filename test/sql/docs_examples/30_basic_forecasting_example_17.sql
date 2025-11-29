-- Create sample data
CREATE TABLE sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Get in-sample fitted values
CREATE TABLE forecast_with_fit AS
SELECT * FROM anofox_fcst_ts_forecast('sales', date, sales, 'AutoETS', 28,
                          MAP{'seasonal_period': 7, 'return_insample': true});

-- Check fit quality
WITH residuals AS (
    SELECT 
        s.sales AS actual,
        UNNEST(f.insample_fitted) AS fitted
    FROM sales s
    CROSS JOIN forecast_with_fit f
    ORDER BY s.date
)
SELECT 
    anofox_fcst_ts_r2(LIST(actual), LIST(fitted)) AS r_squared,
    anofox_fcst_ts_rmse(LIST(actual), LIST(fitted)) AS rmse
FROM residuals;

-- R² > 0.7 indicates good fit
