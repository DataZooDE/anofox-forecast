-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Extend hourly series to target date
SELECT * FROM TS_FILL_FORWARD('hourly_data', series_id, timestamp, value, '2024-12-31'::TIMESTAMP, '1h');

-- Extend monthly series to target date
SELECT * FROM TS_FILL_FORWARD('monthly_data', series_id, date, value, '2024-12-01'::DATE, '1mo');

-- Extend daily series to target date (default frequency)
CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31', '1d'
);

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
SELECT * FROM TS_FILL_FORWARD('daily_data', series_id, date, value, '2024-12-31'::DATE, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Extend series to index 100 with step size of 1
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, 1);

-- Extend series to index 100 with step size of 5
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, 5);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, NULL);

