SELECT * FROM TS_CAP_OUTLIERS_IQR('sales', product_id, date, sales_amount, 1.5);
-- Caps values beyond Q1-1.5*IQR and Q3+1.5*IQR
