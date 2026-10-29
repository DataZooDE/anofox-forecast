LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
WITH cfg AS (SELECT * FROM (VALUES (2000), (10000), (40000)) t(n)),
s AS (
  SELECT n, 'sin_mod97' AS series, TIMESTAMP '2020-01-01' + INTERVAL (i) MINUTE AS ds, sin(i) + (i % 97) AS y FROM cfg, range(n) r(i)
  UNION ALL
  SELECT n, 'mix13', TIMESTAMP '2020-01-01' + INTERVAL (i) MINUTE, sin(i * 1.7) * 3 + cos(i * 0.31) + (i % 13) FROM cfg, range(n) r(i)
),
a AS (SELECT n, series, ts_stats_agg(ds, y) AS st, ts_features_agg(ds, y) AS f FROM s GROUP BY n, series)
SELECT n, series, printf('%.17g', st.entropy) AS stats_entropy,
       printf('%.17g', f.sample_entropy) AS sample_entropy,
       printf('%.17g', f.approximate_entropy) AS approximate_entropy
FROM a ORDER BY n, series;
