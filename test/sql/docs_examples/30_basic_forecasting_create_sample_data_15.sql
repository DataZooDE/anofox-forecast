-- Don't guess - detect!
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(amount ORDER BY date)) AS detected_periods
FROM sales
GROUP BY product_id;

-- Use detected period in forecast
WITH seasonality AS (
    SELECT 
        product_id, 
        CASE 
            WHEN LEN(TS_DETECT_SEASONALITY(LIST(amount ORDER BY date))) > 0 
            THEN TS_DETECT_SEASONALITY(LIST(amount ORDER BY date))[1]
            ELSE NULL 
        END AS primary_period
    FROM sales
    GROUP BY product_id
)
SELECT f.*
FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28,
                    {'seasonal_period': (SELECT primary_period FROM seasonality WHERE product_id = s.product_id)}) f;
