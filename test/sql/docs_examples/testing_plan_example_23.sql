-- Create table with single value
CREATE TABLE single_value_data AS
SELECT 
    DATE '2023-01-01' AS date,
    100.0 AS value;

-- This should handle single value appropriately
SELECT * FROM TS_FORECAST('single_value_data', date, value, 'Naive', 5, MAP{});
-- Expected: Appropriate error or default behavior
