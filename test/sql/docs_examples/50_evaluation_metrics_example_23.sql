-- Evaluate forecast quality over time windows
WITH windows AS (
    SELECT 
        (ROW_NUMBER() OVER (ORDER BY date) - 1) / 30 AS window_id,
        date,
        value
    FROM historical_data
),
forecasts_by_window AS (
    SELECT 
        window_id,
        TS_FORECAST(date, value, 'AutoETS', 7, MAP{'season_length': 7}) AS fc
    FROM windows
    GROUP BY window_id
),
test_by_window AS (
    SELECT 
        window_id,
        LIST(value) AS actual
    FROM test_data
    GROUP BY window_id
)
SELECT 
    t.window_id,
    ROUND(TS_MAE(t.actual, f.fc.point_forecast), 2) AS mae,
    ROUND(TS_MAPE(t.actual, f.fc.point_forecast), 2) AS mape
FROM test_by_window t
JOIN forecasts_by_window f ON t.window_id = f.window_id
ORDER BY t.window_id;
