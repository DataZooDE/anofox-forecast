SELECT * FROM TS_REMOVE_OUTLIERS('test_table', 'date_col', 'value_col', 'id_col', 3.0, 'remove');
-- Verify removes or caps outliers
