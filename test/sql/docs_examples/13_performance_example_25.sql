-- Fast: Only reads 3 columns
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, ...);

-- Slower: Reads all columns first
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales_with_100_columns),  -- Reads everything
    product_id, date, amount, ...
);
