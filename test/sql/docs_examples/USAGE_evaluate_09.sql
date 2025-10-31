-- Check model fitting time
SELECT 
    model_name,
    AVG(fit_time_ms) as avg_fit_time_ms,
    MIN(fit_time_ms) as min_fit_time_ms,
    MAX(fit_time_ms) as max_fit_time_ms
FROM FORECAST('date', 'amount', 'SMA', 10, NULL)
GROUP BY model_name;
