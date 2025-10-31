-- Verify cores being used
SELECT * FROM duckdb_settings() WHERE name = 'threads';
