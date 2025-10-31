-- In-sample fit
WITH fitted AS (
    SELECT * FROM TS_FORECAST('sales_train', date, amount, 'AutoETS', 1,
                              {'return_insample': true, 'seasonal_period': 7})
)
SELECT 
    'In-sample R²' AS metric,
    ROUND(TS_R2(LIST(actual), insample_fitted), 4) AS value
FROM actuals, fitted;

-- Out-of-sample accuracy
SELECT 
    'Out-of-sample MAPE' AS metric,
    ROUND(TS_MAPE(LIST(test_actual), LIST(forecast)), 2) AS value
FROM test_results;

-- Good model: Out-of-sample ≈ In-sample (no overfitting)
