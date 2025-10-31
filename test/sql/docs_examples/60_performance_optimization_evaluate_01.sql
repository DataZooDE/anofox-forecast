-- Single series: Uses 1 core
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- 1,000 series: Uses ALL available cores automatically!
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7});
