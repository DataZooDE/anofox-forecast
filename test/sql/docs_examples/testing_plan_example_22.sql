-- Create empty table
CREATE TABLE empty_data AS
SELECT 
    DATE '2023-01-01' AS date,
    NULL::DOUBLE AS value
WHERE FALSE;

-- This should handle empty data appropriately
SELECT * FROM TS_FORECAST('empty_data', date, value, 'Naive', 5, MAP{});
-- Expected: Appropriate error or empty result
