SELECT * FROM TS_FILL_NULLS_FORWARD('test_table', 'date_col', 'value_col', 'id_col');
SELECT * FROM TS_FILL_NULLS_BACKWARD('test_table', 'date_col', 'value_col', 'id_col');
-- Verify fills NULL values appropriately
