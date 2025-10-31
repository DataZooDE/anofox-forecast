-- Forecast using only data after the last changepoint
WITH last_cp AS (
    SELECT MAX(date_col) AS last_change
    FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
    WHERE is_changepoint = true
)
SELECT * FROM TS_FORECAST(
    (SELECT * FROM sales_data WHERE date > (SELECT last_change FROM last_cp)),
    date, sales, 'AutoETS', 28, {'seasonal_period': 7}
);
