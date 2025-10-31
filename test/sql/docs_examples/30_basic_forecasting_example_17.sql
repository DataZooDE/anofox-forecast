-- Get in-sample fitted values
CREATE TABLE forecast_with_fit AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                          {'seasonal_period': 7, 'return_insample': true});

-- Check fit quality
WITH residuals AS (
    SELECT 
        s.amount AS actual,
        UNNEST(f.insample_fitted) AS fitted
    FROM sales s, forecast_with_fit f
)
SELECT 
    TS_R2(LIST(actual), LIST(fitted)) AS r_squared,
    TS_RMSE(LIST(actual), LIST(fitted)) AS rmse
FROM residuals;

-- R² > 0.7 indicates good fit
