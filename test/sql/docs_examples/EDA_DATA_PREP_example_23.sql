SELECT * FROM TS_REMOVE_OUTLIERS_ZSCORE('sales', product_id, date, sales_amount, 3.0);
-- Removes observations with |z-score| > 3.0
