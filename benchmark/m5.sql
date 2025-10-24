LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
INSTALL httpfs;
LOAD 'httpfs';

CREATE OR REPLACE TABLE m5 AS 
SELECT item_id, CAST(timestamp AS TIMESTAMP) AS timestamp, demand FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;