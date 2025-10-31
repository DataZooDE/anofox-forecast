-- Show how interval width changes with confidence level
WITH ci_80 AS (
    SELECT 'CI 80%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.80, 'seasonal_period': 7})
),
ci_90 AS (
    SELECT 'CI 90%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.90, 'seasonal_period': 7})
),
ci_95 AS (
    SELECT 'CI 95%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.95, 'seasonal_period': 7})
),
ci_99 AS (
    SELECT 'CI 99%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.99, 'seasonal_period': 7})
)
SELECT level, ROUND(width, 2) AS avg_interval_width
FROM ci_80 UNION ALL SELECT * FROM ci_90 
UNION ALL SELECT * FROM ci_95 UNION ALL SELECT * FROM ci_99
ORDER BY avg_interval_width;
