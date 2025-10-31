-- Step 1: Try default
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;

-- Step 2: If too few, decrease hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, {'hazard_lambda': 100.0})
WHERE is_changepoint = true;

-- Step 3: If too many, increase hazard_lambda
SELECT COUNT(*) AS changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, {'hazard_lambda': 500.0})
WHERE is_changepoint = true;
