-- Set up alerts for large residuals
CREATE VIEW forecast_alerts AS
WITH residuals AS (...)
SELECT * FROM residuals
WHERE ABS(residual) > 3 * STDDEV(residual) OVER ();
