-- Enable fitted values
SELECT 
    LEN(insample_fitted) AS n_observations
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                 {'return_insample': true, 'seasonal_period': 7});

-- Use for diagnostics
WITH fitted AS (
    SELECT * FROM TS_FORECAST(..., {'return_insample': true, ...})
)
SELECT 
    TS_R2(LIST(actual), insample_fitted) AS training_r2,
    TS_MAE(LIST(actual), insample_fitted) AS training_mae
FROM actuals, fitted;
