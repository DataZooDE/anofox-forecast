SELECT 
    LEN(insample_fitted) AS num_fitted_values,
    insample_fitted[1:5] AS first_5_fitted
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                 {'return_insample': true, 'seasonal_period': 7});
