SELECT * FROM TS_DROP_CONSTANT('test_table', 'date_col', 'value_col', 'id_col');
SELECT * FROM TS_DROP_ZEROS('test_table', 'date_col', 'value_col', 'id_col');
-- Verify filters out constant/zero series
