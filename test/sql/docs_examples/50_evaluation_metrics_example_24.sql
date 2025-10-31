-- Daily forecast quality check
WITH yesterday_forecast AS (
    SELECT fc.point_forecast[1] AS predicted
    FROM (
        SELECT TS_FORECAST(date, value, 'AutoETS', 1, MAP{}) AS fc
        FROM historical_data
        WHERE date >= CURRENT_DATE - INTERVAL 90 DAY
    )
),
today_actual AS (
    SELECT value AS actual
    FROM real_time_data
    WHERE date = CURRENT_DATE
)
SELECT 
    CURRENT_DATE AS eval_date,
    TS_MAE([actual], [predicted]) AS mae,
    TS_MAPE([actual], [predicted]) AS mape,
    CASE 
        WHEN TS_MAPE([actual], [predicted]) > 10.0 THEN '⚠️ HIGH ERROR'
        WHEN TS_MAPE([actual], [predicted]) > 5.0 THEN '⚡ MEDIUM ERROR'
        ELSE '✅ LOW ERROR'
    END AS quality
FROM today_actual, yesterday_forecast;
