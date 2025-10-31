SELECT * FROM TS_QUALITY_REPORT('test_table', 'date_col', 'value_col', 'id_col');
-- Verify detects: nulls, zeros, constants, gaps
