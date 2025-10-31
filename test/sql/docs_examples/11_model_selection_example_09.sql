SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'SeasonalNaive', 7, {'seasonal_period': 7});
