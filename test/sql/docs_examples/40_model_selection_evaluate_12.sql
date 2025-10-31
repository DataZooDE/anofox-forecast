-- Recommended: AutoETS (fast + accurate)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'AutoETS', 28, {'seasonal_period': 7});

-- Alternative: SeasonalNaive (if speed critical)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'SeasonalNaive', 28, {'seasonal_period': 7});

-- Alternative: AutoARIMA (if accuracy critical)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'AutoARIMA', 28, {'seasonal_period': 7});
