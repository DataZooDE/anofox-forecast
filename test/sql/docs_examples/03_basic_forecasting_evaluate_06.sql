-- Assuming you have actual values for the forecast period
WITH actuals AS (
    SELECT product_id, date, actual_sales
    FROM sales_actuals
),
forecasts AS (
    SELECT product_id, date_col AS date, point_forecast
    FROM ts_forecast_result
)
SELECT 
    f.product_id,
    ROUND(TS_MAE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS mae,
    ROUND(TS_RMSE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS rmse,
    ROUND(TS_MAPE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS mape
FROM forecasts f
JOIN actuals a ON f.product_id = a.product_id AND f.date = a.date
GROUP BY f.product_id;
