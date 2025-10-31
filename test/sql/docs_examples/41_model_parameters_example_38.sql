-- Test sensitivity to alpha in SES
WITH alpha_values AS (
    SELECT 0.1 AS a UNION ALL SELECT 0.2 UNION ALL SELECT 0.3 UNION ALL
    SELECT 0.5 UNION ALL SELECT 0.7 UNION ALL SELECT 0.9
)
SELECT 
    a AS alpha,
    UNNEST(TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': a}).point_forecast) AS forecast
FROM data, alpha_values
GROUP BY a;
