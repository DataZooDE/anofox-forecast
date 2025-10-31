CREATE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_raw',        -- Input table
    product_id,         -- Group column
    date,               -- Date column
    sales_amount,       -- Value column
    30,                 -- Minimum length
    'forward'           -- Fill method: 'forward', 'mean', 'interpolate', 'zero'
);

-- Pipeline steps:
-- 1. Fill time gaps
-- 2. Drop constant series
-- 3. Drop short series (< min_length)
-- 4. Drop leading zeros
-- 5. Fill remaining nulls (using specified method)
