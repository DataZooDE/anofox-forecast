-- Seasonality
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_raw
GROUP BY product_id;

-- Changepoints (regime changes)
SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('sales_raw', product_id, date, sales_amount,
                                         {'include_probabilities': true});
