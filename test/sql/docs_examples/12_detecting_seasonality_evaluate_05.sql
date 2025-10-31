-- Detect seasonality and use it for forecasting
WITH detection AS (
    SELECT TS_DETECT_SEASONALITY(LIST(sales ORDER BY date))[1] AS period
    FROM sales_data
)
SELECT * FROM TS_FORECAST(
    'sales_data',
    date,
    sales,
    'SeasonalNaive',
    28,
    {'seasonal_period': (SELECT period FROM detection)}
);
