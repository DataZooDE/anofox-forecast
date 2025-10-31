SELECT * FROM TS_DETECT_GAPS('test_table', 'date_col', 'value_col', 'id_col', 'DAY');
-- Verify detects missing timestamps
