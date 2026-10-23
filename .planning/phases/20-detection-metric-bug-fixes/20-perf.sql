-- Phase 20 performance workload (20-02 baseline, compared in 20-05).
-- Run: env -u AI_AGENT <duckdb-binary> -unsigned < .planning/phases/20-detection-metric-bug-fixes/20-perf.sql
-- 1M-row panel: 5000 series x 200 days. `bigu` is the same rows as a UNION ALL
-- (literal cut-off branch + scalar-subquery cut-off branch), the #285 plan shape.

CREATE TABLE big AS
SELECT
    's' || (i // 200) AS id,
    TIMESTAMP '2020-01-01' + INTERVAL (i % 200) DAY AS ds,
    100 + (i % 200) * 0.1 + 10 * sin(2 * pi() * (i % 200) / 7) + ((i * 7919) % 101) / 50.0 AS y,
    CAST(100 + (i % 200) * 0.1 + 10 * sin(2 * pi() * (i % 200) / 7) + ((i * 7919) % 101) / 50.0 + 1 AS DOUBLE) AS f
FROM range(1000000) r(i);

CREATE VIEW bigu AS
SELECT * FROM big WHERE ds < TIMESTAMP '2020-05-01'
UNION ALL
SELECT * FROM big WHERE ds >= (SELECT min(ds) FROM big WHERE ds >= TIMESTAMP '2020-05-01');

.timer on

SELECT 'forecast_by_theta_big', count(*) FROM (SELECT * FROM ts_forecast_by('big', id, ds, y, 'Theta', 14, '1d', params := MAP{'seasonal_period': '7'}));
SELECT 'forecast_by_theta_bigu', count(*) FROM (SELECT * FROM ts_forecast_by('bigu', id, ds, y, 'Theta', 14, '1d', params := MAP{'seasonal_period': '7'}));
SELECT 'forecast_by_naive_big', count(*) FROM (SELECT * FROM ts_forecast_by('big', id, ds, y, 'Naive', 14, '1d'));
SELECT 'native_naive_big', count(*) FROM (SELECT * FROM _ts_forecast_native((SELECT id, ds, y FROM big), 14, '1d', 'Naive', MAP{}));
SELECT 'native_theta_big', count(*) FROM (SELECT * FROM _ts_forecast_native((SELECT id, ds, y FROM big), 14, '1d', 'Theta', MAP{'seasonal_period': '7'}));
SELECT 'mae_by_big', count(*) FROM (SELECT * FROM ts_mae_by((SELECT id, ds, y, f FROM big), 'ds', 'y', 'f'));
SELECT 'mase_by_big', count(*) FROM (SELECT * FROM ts_mase_by((SELECT id, ds, y, CASE WHEN ds >= TIMESTAMP '2020-07-01' THEN f END AS f FROM big), 'ds', 'y', 'f', seasonal_period := 7));
SELECT 'stats_by_big', count(*) FROM (SELECT * FROM ts_stats_by('big', id, ds, y, '1d'));
SELECT 'fill_gaps_by_big', count(*) FROM (SELECT * FROM ts_fill_gaps_by('big', id, ds, y, '1d'));

SET threads = 1;
SELECT 'forecast_by_theta_big_threads1', count(*) FROM (SELECT * FROM ts_forecast_by('big', id, ds, y, 'Theta', 14, '1d', params := MAP{'seasonal_period': '7'}));
