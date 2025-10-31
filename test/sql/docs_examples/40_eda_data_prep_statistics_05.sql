-- Seasonality
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_raw', product_id, date, sales_amount);

-- Changepoints (regime changes)
SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('sales_raw', product_id, date, sales_amount,
                                         {'include_probabilities': true});
