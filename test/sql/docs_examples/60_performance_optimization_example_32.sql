-- Create performance log
CREATE TABLE forecast_performance_log (
    run_date DATE,
    num_series INT,
    model_used VARCHAR,
    execution_time_sec DOUBLE,
    memory_mb DOUBLE
);

-- Log each run
INSERT INTO forecast_performance_log
SELECT 
    CURRENT_DATE,
    (SELECT COUNT(DISTINCT product_id) FROM sales),
    'AutoETS',
    15.7,  -- Measured execution time
    2048   -- Measured memory
);

-- Monitor trends
SELECT 
    run_date,
    execution_time_sec,
    num_series,
    ROUND(execution_time_sec / num_series * 1000, 2) AS ms_per_series
FROM forecast_performance_log
WHERE run_date >= CURRENT_DATE - INTERVAL '30 days'
ORDER BY run_date;
