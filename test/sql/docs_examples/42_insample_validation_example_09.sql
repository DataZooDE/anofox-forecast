-- Per-series diagnostics
WITH fc AS (
    SELECT * FROM TS_FORECAST_BY('multi_sales', product_id, date, amount, 
                                 'AutoETS', 28,
                                 {'return_insample': true, 
                                  'confidence_level': 0.95,
                                  'seasonal_period': 7})
),
fit_quality AS (
    SELECT 
        product_id,
        LEN(insample_fitted) AS n_obs,
        confidence_level,
        model_name
    FROM fc
)
SELECT 
    product_id,
    n_obs AS training_points,
    confidence_level * 100 || '%' AS conf_level,
    model_name
FROM fit_quality
WHERE n_obs >= 50  -- Only well-trained models
ORDER BY product_id;
