CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31'
);
