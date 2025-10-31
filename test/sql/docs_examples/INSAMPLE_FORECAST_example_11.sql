-- Test if a model returns fitted values
SELECT 
    model_name,
    LEN(insample_fitted) AS fitted_count,
    CASE 
        WHEN LEN(insample_fitted) > 0 
        THEN '✅ Supported'
        ELSE '❌ Not supported'
    END AS status
FROM TS_FORECAST('sales', date, amount, 'ModelName', 7, 
                 {'return_insample': true});
