-- Let AutoETS select best configuration
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- Or specify manually
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 0,      -- 0=additive
    'trend_type': 1,      -- 1=additive
    'season_type': 1      -- 1=additive
});
