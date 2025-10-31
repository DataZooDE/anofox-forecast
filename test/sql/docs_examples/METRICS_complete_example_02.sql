-- Generate forecasts for multiple products
CREATE TEMP TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY(
    'sales',
    product_id,
    date,
    revenue,
    'AutoETS',
    30,
    MAP{'season_length': 7}
);

-- Join with actuals and evaluate per product
CREATE TEMP TABLE evaluation AS
SELECT 
    f.product_id,
    a.actual_value AS actual,
    f.point_forecast AS predicted
FROM forecasts f
JOIN actuals a ON f.product_id = a.product_id AND f.forecast_step = a.step;

-- Calculate metrics per product using GROUP BY + LIST()
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(predicted)) AS mae,
    TS_RMSE(LIST(actual), LIST(predicted)) AS rmse,
    TS_MAPE(LIST(actual), LIST(predicted)) AS mape,
    TS_BIAS(LIST(actual), LIST(predicted)) AS bias
FROM evaluation
GROUP BY product_id
ORDER BY mae;  -- Best forecasts first
