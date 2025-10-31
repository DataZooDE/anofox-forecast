-- Compare models
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'SeasonalNaive', 14, {'seasonal_period': 7});
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'Theta', 14, {'seasonal_period': 7});
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'ARIMA', 14, {'p': 1, 'd': 0, 'q': 1});
