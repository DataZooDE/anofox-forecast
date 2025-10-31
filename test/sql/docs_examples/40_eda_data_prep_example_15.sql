-- Detect
WITH end_dates AS (
    SELECT 
        MAX(end_date) AS latest_date,
        COUNT(DISTINCT end_date) AS n_different_ends
    FROM sales_stats
)
SELECT * FROM end_dates;

-- Fix: Extend all series to common date
CREATE TABLE sales_aligned AS
SELECT * FROM TS_FILL_FORWARD(
    'sales',
    product_id,
    date,
    sales_amount,
    (SELECT MAX(date) FROM sales)  -- Latest date
);
