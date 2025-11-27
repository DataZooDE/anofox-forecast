-- Backward fill (use next known value)
CREATE TABLE sales_backward_filled AS
SELECT * FROM TS_FILL_NULLS_BACKWARD('sales', product_id, date, sales_amount);

