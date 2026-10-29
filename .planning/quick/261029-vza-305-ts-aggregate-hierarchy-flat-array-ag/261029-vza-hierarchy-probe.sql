LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
CREATE TABLE src AS
SELECT DATE '2024-01-01' + d::INTEGER AS ds, (s % 37) * 1.25 + d * 0.5 + sin(s + d) AS y,
       'R' || (s % 10) AS region, 'S' || (s % 200) AS store, 'I' || s AS sid
FROM range(10000) t(s), range(365) r(d);
.timer on
CREATE TABLE out_native AS SELECT * FROM ts_aggregate_hierarchy((SELECT ds, y, region, store, sid FROM src), MAP{});
.timer off
SELECT count(*), sum(hash(unique_id, ds, y)) FROM out_native;
-- order-sensitive: row position included; NULL values, NULL keys, integer keys, timestamp dates
CREATE TABLE small AS
SELECT TIMESTAMP '2024-01-01' + INTERVAL (d) HOUR AS ds,
       CASE WHEN (s + d) % 11 = 0 THEN NULL ELSE 0.1 * s + d / 7.0 END AS y,
       CASE WHEN s % 13 = 0 THEN NULL ELSE s % 4 END AS grp, 'k' || s AS item
FROM range(60) t(s), range(50) r(d);
SELECT sum(hash(rn, unique_id, ds, y)), count(*), count(y) FROM (SELECT row_number() OVER () AS rn, * FROM ts_aggregate_hierarchy((SELECT ds, y, grp, item FROM small), MAP{'separator': '/', 'aggregate_keyword': 'ALL'}));
SELECT * FROM ts_aggregate_hierarchy((SELECT ds, y, grp, item FROM small), MAP{'separator': '/', 'aggregate_keyword': 'ALL'}) LIMIT 4;
