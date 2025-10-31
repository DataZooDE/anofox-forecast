CREATE TABLE sales_no_edge_zeros AS
SELECT * FROM TS_DROP_EDGE_ZEROS('sales', product_id, date, sales_amount);
