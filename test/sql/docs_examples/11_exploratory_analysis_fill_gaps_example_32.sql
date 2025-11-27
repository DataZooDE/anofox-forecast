-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Fill gaps with daily frequency (default)
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Fill gaps with 30-minute frequency
SELECT * FROM TS_FILL_GAPS('hourly_data', series_id, timestamp, value, '30m');

-- Fill gaps with weekly frequency
SELECT * FROM TS_FILL_GAPS('weekly_data', series_id, date, value, '1w');

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
SELECT * FROM TS_FILL_GAPS('daily_data', series_id, date, value, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Fill gaps with step size of 1
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, 1);

-- Fill gaps with step size of 2
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, 2);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, NULL);

