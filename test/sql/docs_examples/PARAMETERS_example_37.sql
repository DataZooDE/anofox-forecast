-- Compare seasonal models
WITH seasonal_forecasts AS (
    SELECT 'SeasonalNaive' AS model, 
           TS_FORECAST(date, sales, 'SeasonalNaive', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'SeasonalES',
           TS_FORECAST(date, sales, 'SeasonalES', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'HoltWinters',
           TS_FORECAST(date, sales, 'HoltWinters', 12, MAP{'seasonal_period': 7}) AS fc
    FROM sales_data
    UNION ALL
    SELECT 'AutoETS',
           TS_FORECAST(date, sales, 'AutoETS', 12, MAP{'season_length': 7}) AS fc
    FROM sales_data
)
SELECT model, UNNEST(fc.point_forecast) AS forecast
FROM seasonal_forecasts;
