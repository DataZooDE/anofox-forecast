-- Split data into train/test
CREATE TABLE train AS
SELECT * FROM sales WHERE date < DATE '2023-10-01';

CREATE TABLE test AS
SELECT * FROM sales WHERE date >= DATE '2023-10-01' AND date < DATE '2023-11-01';

-- Generate forecasts from different models
WITH models AS (
    VALUES 
        ('AutoETS'),
        ('AutoARIMA'),
        ('Theta'),
        ('SeasonalNaive')
),
forecasts AS (
    SELECT 
        m.column0 AS model_name,
        fc.*
    FROM models m
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            'train', date, amount, m.column0, 30,
            CASE 
                WHEN m.column0 = 'SeasonalNaive' THEN {'seasonal_period': 7}
                WHEN m.column0 = 'Theta' THEN {'seasonal_period': 7}
                WHEN m.column0 IN ('AutoETS', 'AutoARIMA') THEN {'seasonal_period': 7}
                ELSE MAP{}
            END
        )
    ) fc
),
evaluation AS (
    SELECT 
        f.model_name,
        TS_MAE(LIST(t.amount), LIST(f.point_forecast)) AS mae,
        TS_RMSE(LIST(t.amount), LIST(f.point_forecast)) AS rmse,
        TS_MAPE(LIST(t.amount), LIST(f.point_forecast)) AS mape,
        TS_COVERAGE(LIST(t.amount), LIST(f.lower), LIST(f.upper)) AS coverage
    FROM forecasts f
    JOIN test t ON f.date_col = t.date
    GROUP BY f.model_name
)
SELECT 
    model_name,
    ROUND(mae, 2) AS mae,
    ROUND(rmse, 2) AS rmse,
    ROUND(mape, 2) AS mape_pct,
    ROUND(coverage * 100, 1) AS coverage_pct,
    CASE 
        WHEN mae = MIN(mae) OVER () THEN '🌟 Best'
        ELSE ''
    END AS recommendation
FROM evaluation
ORDER BY mae;
