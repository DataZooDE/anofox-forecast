-- Seasonal data with trend
SELECT * FROM TS_FORECAST('sales', date, amount, 'HoltWinters', 28, {
    'seasonal_period': 7,
    'multiplicative': false,  -- Additive seasonality
    'alpha': 0.2,
    'beta': 0.1,
    'gamma': 0.3
});
