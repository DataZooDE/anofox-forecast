-- Compare in-sample vs out-of-sample performance
WITH training AS (
    SELECT * FROM sales WHERE date < DATE '2023-06-01'
),
test AS (
    SELECT * FROM sales WHERE date >= DATE '2023-06-01'
),
fc AS (
    SELECT * FROM TS_FORECAST('training', date, amount, 'AutoETS', 30,
                              {'return_insample': true, 'seasonal_period': 7})
),
insample_error AS (
    SELECT 
        TS_MAE(LIST(t.amount ORDER BY t.date), 
               LIST(UNNEST(fc.insample_fitted))) AS mae_train
    FROM training t, fc
),
outsample_error AS (
    SELECT 
        TS_MAE(LIST(test.amount ORDER BY test.date), 
               LIST(fc.point_forecast ORDER BY fc.forecast_step)) AS mae_test
    FROM test
    JOIN fc ON test.date = fc.date_col
)
SELECT 
    ROUND(i.mae_train, 2) AS mae_training,
    ROUND(o.mae_test, 2) AS mae_test,
    CASE 
        WHEN o.mae_test > i.mae_train * 1.5
        THEN '⚠️ Possible overfitting'
        WHEN o.mae_test > i.mae_train * 1.2
        THEN '⚠️ Monitor closely'
        ELSE '✓ Good generalization'
    END AS assessment
FROM insample_error i, outsample_error o;
