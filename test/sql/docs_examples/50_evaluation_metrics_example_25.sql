-- Compare 5 models, select best by MAE
WITH models AS (
    SELECT 'Naive' AS m, TS_FORECAST(...) AS fc FROM data
    UNION ALL SELECT 'SMA', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'Theta', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'AutoETS', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'AutoARIMA', TS_FORECAST(...) FROM data
)
SELECT 
    m AS model,
    TS_MAE(actual_list, fc.point_forecast) AS mae
FROM models, test_data
ORDER BY mae
LIMIT 1;  -- Best model
