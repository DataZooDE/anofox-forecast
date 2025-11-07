-- Test AutoMFLES directly in DuckDB shell
INSTALL '/home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
LOAD anofox_forecast;

-- Create a simple test table
CREATE TABLE test_series AS
SELECT
    'D1' as unique_id,
    DATE '2020-01-01' + INTERVAL (n) DAY as ds,
    1000 + (n % 100) * 10.0 as y
FROM generate_series(0, 1005) as t(n);

-- Test AutoMFLES (should trigger our logging)
SELECT * FROM TS_FORECAST_BY(
    'test_series',
    unique_id,
    ds,
    y,
    'AutoMFLES',
    14,
    {'seasonal_periods': [7]}
)
LIMIT 5;
