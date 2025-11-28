-- Create table with all NULL values
CREATE TABLE null_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    NULL::DOUBLE AS value
FROM generate_series(0, 2) t(d);

-- This should handle all NULL values appropriately
SELECT * FROM TS_FORECAST('null_data', date, value, 'Naive', 5, MAP{});
-- Expected: Appropriate error
