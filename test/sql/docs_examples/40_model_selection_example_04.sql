-- Manual ETS
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 0,      -- Additive errors
    'trend_type': 1,      -- Additive trend
    'season_type': 1      -- Additive seasonality
});

-- Or let AutoETS choose
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});
