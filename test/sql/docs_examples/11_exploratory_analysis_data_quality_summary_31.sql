-- Get summary by dimension (n_short parameter defaults to 30 if NULL)
SELECT * FROM TS_DATA_QUALITY_SUMMARY('sales_raw', product_id, date, sales_amount, 30);

