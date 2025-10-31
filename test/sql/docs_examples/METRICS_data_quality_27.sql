-- Compare two model versions
WITH version_a AS (
    SELECT TS_FORECAST(..., 'Theta', ...) AS fc FROM data
),
version_b AS (
    SELECT TS_FORECAST(..., 'AutoETS', ...) AS fc FROM data
)
SELECT 
    'Version A' AS version,
    TS_MAE(actual, version_a.fc.point_forecast) AS mae,
    TS_MAPE(actual, version_a.fc.point_forecast) AS mape
FROM test_data, version_a
UNION ALL
SELECT 
    'Version B',
    TS_MAE(actual, version_b.fc.point_forecast),
    TS_MAPE(actual, version_b.fc.point_forecast)
FROM test_data, version_b;
