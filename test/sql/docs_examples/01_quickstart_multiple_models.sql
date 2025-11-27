-- Compare forecasts from different models using the same dataset
-- Note: Assumes 'my_sales' table exists from previous step

-- Model 1: AutoETS (automatic exponential smoothing)
SELECT 'AutoETS' AS model_name, * FROM TS_FORECAST(
    'my_sales', date, sales, 'AutoETS', 14, {'seasonal_period': 7}
) LIMIT 5;

-- Model 2: SeasonalNaive (seasonal naive method)
SELECT 'SeasonalNaive' AS model_name, * FROM TS_FORECAST(
    'my_sales', date, sales, 'SeasonalNaive', 14, {'seasonal_period': 7}
) LIMIT 5;

-- Model 3: Theta (theta decomposition method)
SELECT 'Theta' AS model_name, * FROM TS_FORECAST(
    'my_sales', date, sales, 'Theta', 14, {'seasonal_period': 7}
) LIMIT 5;

