-- Every query re-analyzes data
SELECT * FROM TS_STATS('sales', product_id, date, amount);
