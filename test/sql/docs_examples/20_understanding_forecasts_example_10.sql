-- Get fitted values
CREATE TABLE with_fitted AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                          {'seasonal_period': 7, 'return_insample': true});

-- Compute residuals
WITH actuals_fitted AS (
    SELECT 
        s.amount AS actual,
        UNNEST(f.insample_fitted) AS fitted,
        ROW_NUMBER() OVER (ORDER BY s.date) AS idx
    FROM sales s, with_fitted f
)
SELECT 
    idx,
    actual,
    ROUND(fitted, 2) AS fitted,
    ROUND(actual - fitted, 2) AS residual,
    ROUND((actual - fitted) / NULLIF(STDDEV(actual - fitted) OVER (), 0), 2) AS standardized_residual
FROM actuals_fitted;
