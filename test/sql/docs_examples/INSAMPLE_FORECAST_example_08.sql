-- Rolling window validation
CREATE TABLE cv_results AS
WITH windows AS (
    SELECT 
        w AS window_id,
        DATE '2023-01-01' + INTERVAL (w * 7) DAY AS train_end
    FROM generate_series(1, 10) t(w)
),
forecasts AS (
    SELECT 
        w.window_id,
        w.train_end,
        fc.*
    FROM windows w
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            (SELECT date, amount FROM sales WHERE date <= w.train_end),
            date, amount, 'AutoETS', 7,
            {'return_insample': true, 'seasonal_period': 7}
        )
    ) fc
)
SELECT 
    window_id,
    train_end,
    LEN(insample_fitted) AS train_size,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    confidence_level
FROM forecasts
GROUP BY window_id, train_end, confidence_level
ORDER BY window_id;

-- Summarize CV performance
SELECT 
    AVG(train_size) AS avg_train_size,
    STDDEV(avg_forecast) AS forecast_stability
FROM cv_results;
