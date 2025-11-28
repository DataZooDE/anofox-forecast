-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Step 1: Try default
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;

-- Step 2: If too few, decrease hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{'hazard_lambda': 100.0})
WHERE is_changepoint = true;

-- Step 3: If too many, increase hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{'hazard_lambda': 500.0})
WHERE is_changepoint = true;
