-- Let AutoARIMA select parameters
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7});

-- Or specify manually
SELECT * FROM TS_FORECAST('sales', date, amount, 'ARIMA', 28, {
    'p': 1, 'd': 1, 'q': 1,
    'P': 1, 'D': 1, 'Q': 1, 's': 7
});
