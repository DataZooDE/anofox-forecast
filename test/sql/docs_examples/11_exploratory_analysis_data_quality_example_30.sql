-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Generate comprehensive health card (n_short parameter defaults to 30 if NULL)
CREATE TABLE health_card AS
SELECT * FROM TS_DATA_QUALITY('sales_raw', product_id, date, sales_amount, 30, '1d');

-- View all issues
SELECT * FROM health_card ORDER BY dimension, metric;

-- Filter specific issues
SELECT * FROM TS_DATA_QUALITY('sales', product_id, date, amount, 30, '1d')
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps';

-- INTEGER columns: Use INTEGER frequency values
SELECT * FROM TS_DATA_QUALITY('int_data', series_id, date_col, value, 30, 1)
WHERE dimension = 'Magnitude' AND metric = 'missing_values';

