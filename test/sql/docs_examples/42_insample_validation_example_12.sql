-- Always check fitted values quality before deploying
WITH fc AS (
    SELECT * FROM TS_FORECAST(..., {'return_insample': true})
)
SELECT 
    TS_R2(...) AS r_squared,
    TS_RMSE(...) AS rmse
FROM fc
WHERE TS_R2(...) > 0.7;  -- Ensure good fit
