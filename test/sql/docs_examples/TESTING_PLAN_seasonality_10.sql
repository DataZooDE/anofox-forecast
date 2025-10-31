SELECT * FROM TS_DETECT_OUTLIERS('test_table', 'date_col', 'value_col', 'id_col', 3.0);
-- Verify flags outliers based on threshold
