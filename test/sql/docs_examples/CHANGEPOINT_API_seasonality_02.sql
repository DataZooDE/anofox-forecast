-- Detect with default parameters
SELECT *
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
WHERE is_changepoint = true;

-- More sensitive detection
SELECT *
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, {'hazard_lambda': 50.0})
WHERE is_changepoint = true;

-- With probabilities for confidence scoring
SELECT date_col, is_changepoint, ROUND(changepoint_probability, 4) AS confidence
FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, {'include_probabilities': true})
WHERE is_changepoint = true
ORDER BY changepoint_probability DESC;
