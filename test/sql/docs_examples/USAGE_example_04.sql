SELECT * FROM FORECAST('timestamp', 'sales', 'Naive', 12, NULL)
ORDER BY forecast_step;
