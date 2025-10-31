-- Forecast data growth for storage planning
WITH daily_data_growth AS (
    SELECT 
        date,
        SUM(table_size_mb) AS total_size_mb
    FROM table_sizes_history
    GROUP BY date
),
growth_forecast AS (
    SELECT * FROM TS_FORECAST('daily_data_growth', date, total_size_mb,
                              'Holt', 180,  -- 6 months ahead
                              MAP{})  -- Growth model (trend, no seasonality)
)
SELECT 
    DATE_TRUNC('month', date_col) AS month,
    ROUND(MAX(upper) / 1024.0, 2) AS storage_needed_gb,
    ROUND(MAX(upper) / 1024.0 - (SELECT MAX(total_size_mb) / 1024.0 FROM daily_data_growth), 2) AS additional_storage_gb,
    CASE 
        WHEN MAX(upper) / 1024.0 > 1000 THEN '⚠️ Plan storage expansion'
        ELSE '✓ Current capacity sufficient'
    END AS recommendation
FROM growth_forecast
GROUP BY month
ORDER BY month;
