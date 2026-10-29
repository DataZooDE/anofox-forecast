LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
CREATE TEMP TABLE t10k AS SELECT TIMESTAMP '2020-01-01' + INTERVAL (i) MINUTE AS ds, sin(i) + (i % 97) AS y FROM range(10000) r(i);
CREATE TEMP TABLE t40k AS SELECT TIMESTAMP '2020-01-01' + INTERVAL (i) MINUTE AS ds, sin(i) + (i % 97) AS y FROM range(40000) r(i);
CREATE TEMP TABLE t160k AS SELECT TIMESTAMP '2020-01-01' + INTERVAL (i) MINUTE AS ds, sin(i) + (i % 97) AS y FROM range(160000) r(i);
.timer on
SELECT '10k', (ts_stats_agg(ds, y)).entropy FROM t10k;
SELECT '40k', (ts_stats_agg(ds, y)).entropy FROM t40k;
SELECT '160k', (ts_stats_agg(ds, y)).entropy FROM t160k;
