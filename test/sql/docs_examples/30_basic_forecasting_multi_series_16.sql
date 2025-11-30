-- Create sample data
CREATE TABLE sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Step 1: Start with AutoETS
SELECT * FROM anofox_fcst_ts_forecast('sales', date, sales, 'AutoETS', 28, MAP{'seasonal_period': 7});

-- Step 2: If needed, try specialized models
SELECT * FROM anofox_fcst_ts_forecast('sales', date, sales, 'AutoARIMA', 28, MAP{'seasonal_period': 7});

-- Step 3: Fine-tune parameters
SELECT * FROM anofox_fcst_ts_forecast('sales', date, sales, 'ETS', 28, 
                          MAP{'seasonal_period': 7, 'trend_type': 2, 'season_type': 1});
