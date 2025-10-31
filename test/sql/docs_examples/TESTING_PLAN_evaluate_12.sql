SELECT * FROM TS_FILL_GAPS('test_table', 'date_col', 'value_col', 'id_col', 'DAY', 'linear');
-- Verify fills missing timestamps with interpolated values
