-- Fastest forecast
SELECT * FROM TS_FORECAST('sales', date, amount, 'SeasonalNaive', 28, {'seasonal_period': 7});
