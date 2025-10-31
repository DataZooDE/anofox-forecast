-- Cross-validation with expanding window
WITH cv_windows AS (
    SELECT w AS window_id
    FROM generate_series(30, 330, 30) t(w)  -- Every 30 days
),
cv_forecasts AS (
    SELECT 
        window_id,
        fc.*
    FROM cv_windows w
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE EPOCH(date) / 86400 <= window_id),
            date, amount, 'AutoETS', 30, {'seasonal_period': 7}
        )
    ) fc
)
SELECT 
    window_id AS train_days,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    STDDEV(point_forecast) AS forecast_volatility
FROM cv_forecasts
GROUP BY window_id
ORDER BY window_id;
