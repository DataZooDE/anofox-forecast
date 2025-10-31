-- Detect seasonality automatically
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount);

-- Analyze seasonal strength
SELECT 
    product_id,
    detected_periods,
    primary_period,
    is_seasonal
FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount)
WHERE is_seasonal = true;
