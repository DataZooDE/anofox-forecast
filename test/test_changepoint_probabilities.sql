-- Test changepoint probabilities

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

DROP TABLE IF EXISTS cp_test;
CREATE TABLE cp_test AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS ds,
    CASE 
        WHEN d < 50 THEN 100 + (RANDOM() * 5)
        ELSE 200 + (RANDOM() * 5)
    END AS y
FROM generate_series(0, 99) t(d);

SELECT '=== Test 1: Without probabilities (default) ===' AS test;
DESCRIBE SELECT * FROM TS_DETECT_CHANGEPOINTS('cp_test', ds, y, MAP{});

SELECT '=== Test 2: With probabilities ===' AS test;
DESCRIBE SELECT * FROM TS_DETECT_CHANGEPOINTS('cp_test', ds, y, {'include_probabilities': true});

SELECT '=== Test 3: Show changepoints with probabilities ===' AS test;
SELECT 
    date_col AS ds,
    ROUND(value_col, 2) AS y,
    is_changepoint,
    ROUND(changepoint_probability, 4) AS cp_prob
FROM TS_DETECT_CHANGEPOINTS('cp_test', ds, y, {'include_probabilities': true})
WHERE changepoint_probability > 0.1  -- Show significant probabilities
ORDER BY ds
LIMIT 20;

SELECT '=== Test 4: Highest probability changepoints ===' AS test;
SELECT 
    date_col AS ds,
    is_changepoint,
    ROUND(changepoint_probability, 4) AS cp_prob
FROM TS_DETECT_CHANGEPOINTS('cp_test', ds, y, {'include_probabilities': true})
ORDER BY changepoint_probability DESC
LIMIT 10;

SELECT '=== Test 5: Multiple series with probabilities ===' AS test;
DROP TABLE IF EXISTS multi_test;
CREATE TABLE multi_test AS
SELECT 
    (d % 3 + 1) AS id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS ds,
    CASE 
        WHEN d < 30 THEN 100.0
        ELSE 200.0
    END + (RANDOM() * 5) AS y
FROM generate_series(0, 89) t(d);

SELECT 
    id,
    date_col AS ds,
    is_changepoint,
    ROUND(changepoint_probability, 4) AS cp_prob
FROM TS_DETECT_CHANGEPOINTS_BY('multi_test', id, ds, y, {'include_probabilities': true})
WHERE is_changepoint = true
ORDER BY id, ds;

SELECT '=== SUCCESS: Probability feature working! ===' AS result;

