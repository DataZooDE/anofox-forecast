-- Future implementation might look like:
SELECT * FROM TS_FORECAST('sales', date, amount, 'Ensemble', 28, {
    'models': ['AutoETS', 'AutoARIMA', 'Theta'],
    'weights': [0.5, 0.3, 0.2],  -- Or 'auto' for automatic weighting
    'seasonal_period': 7
});
