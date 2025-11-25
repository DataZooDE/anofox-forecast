-- Detect seasonality automatically
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(amount ORDER BY date)) AS detected_periods
FROM sales
GROUP BY product_id;

-- Analyze seasonal strength
WITH seasonality AS (
    SELECT 
        product_id,
        TS_DETECT_SEASONALITY(LIST(amount ORDER BY date)) AS detected_periods
    FROM sales
    GROUP BY product_id
)
SELECT 
    product_id,
    detected_periods,
    CASE 
        WHEN LEN(detected_periods) > 0 THEN detected_periods[1]
        ELSE NULL 
    END AS primary_period,
    LEN(detected_periods) > 0 AS is_seasonal
FROM seasonality
WHERE LEN(detected_periods) > 0;
