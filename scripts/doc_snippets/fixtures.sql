-- Deterministic synthetic fixtures for scripts/check_doc_snippets.py (Phase 18 plan
-- 18-04, D-06). Union of the phase-local snippet-harness forks (quick task 261009-d02,
-- then .planning/phases/{10,12,13,14,15,16,17}-*/snippets/fixtures.sql); the Phase 17
-- fork was already a strict superset of every earlier fork's table names, so this file
-- is that fork carried forward as the permanent copy. Uses range()/sin()/modulo
-- arithmetic only -- never random() -- so results are reproducible across runs. Grown
-- iteratively as doc examples need new table/column names; never papers over a real
-- doc bug (unknown parameter, binder error, wrong output column) with a fixture.

LOAD anofox_forecast;
LOAD json;

-- monthly_revenue(store_id, date, revenue): 2 groups x 130 daily rows, trend + period-7 seasonality.
CREATE TABLE monthly_revenue AS
SELECT
    store_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    100.0 + store_id * 10.0 + 0.5 * i + 10.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1 AS revenue
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS store_id);

-- quarterly_sales(region_id, date, sales): 2 groups x 130 daily rows, trend + period-7 seasonality.
CREATE TABLE quarterly_sales AS
SELECT
    region_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    200.0 + region_id * 15.0 + 0.3 * i + 8.0 * sin(2 * pi() * i / 7.0) + (i % 4) * 0.2 AS sales
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region_id);

-- daily_sales(product_id, date, quantity, value, revenue): 2 groups x 130 daily rows,
-- trend + period-7 seasonality. Multiple value-column aliases because different doc sections
-- reference the same table name with different target columns (pre-existing, out of scope).
CREATE TABLE daily_sales AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    (50.0 + product_id * 5.0 + 0.4 * i + 6.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS quantity,
    (50.0 + product_id * 5.0 + 0.4 * i + 6.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS value,
    (50.0 + product_id * 5.0 + 0.4 * i + 6.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) * 10.0 AS revenue
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- daily_visits(store_id, date, visitors): 2 groups x 130 daily rows, period-7 seasonality.
CREATE TABLE daily_visits AS
SELECT
    store_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    200.0 + store_id * 10.0 + 15.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.2 AS visitors
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS store_id);

-- daily_inventory(sku_id, date, stock_level): 2 groups x 130 daily rows, no trend.
CREATE TABLE daily_inventory AS
SELECT
    sku_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    500.0 + sku_id * 20.0 - 2.0 * (i % 10) + sin(2 * pi() * i / 7.0) * 5.0 AS stock_level
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sku_id);

-- spare_parts_orders(part_id, date, quantity): 2 groups x 130 daily rows, intermittent (many zeros).
CREATE TABLE spare_parts_orders AS
SELECT
    part_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    CASE WHEN (i + part_id) % 6 = 0 THEN 3.0 + (i % 4) ELSE 0.0 END AS quantity
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS part_id);

-- spare_parts_demand(part_id, date, quantity): 2 groups x 130 daily rows, intermittent (many zeros).
CREATE TABLE spare_parts_demand AS
SELECT
    part_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    CASE WHEN (i + part_id) % 5 = 0 THEN 2.0 + (i % 3) ELSE 0.0 END AS quantity
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS part_id);

-- intermittent_sales(product_id, date, units): 2 groups x 130 daily rows, intermittent (many zeros).
CREATE TABLE intermittent_sales AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    CASE WHEN (i + product_id) % 6 = 0 THEN 1.0 + (i % 3) ELSE 0.0 END AS units
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- obsolete_items(sku_id, date, demand): 2 groups x 130 daily rows, declining intermittent demand.
CREATE TABLE obsolete_items AS
SELECT
    sku_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    CASE WHEN (i + sku_id) % 8 = 0 THEN GREATEST(0.0, 5.0 - 0.03 * i) ELSE 0.0 END AS demand
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sku_id);

-- weekly_data(store_id, date, revenue): 2 groups x 130 daily rows, period-7 seasonality.
CREATE TABLE weekly_data AS
SELECT
    store_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    300.0 + store_id * 20.0 + 0.4 * i + 12.0 * sin(2 * pi() * i / 7.0) AS revenue
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS store_id);

-- weekly_patterns(location_id, date, traffic): 2 groups x 130 daily rows, period-7 seasonality.
CREATE TABLE weekly_patterns AS
SELECT
    location_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    150.0 + location_id * 10.0 + 10.0 * sin(2 * pi() * i / 7.0) AS traffic
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS location_id);

-- sensor_data(sensor_id, timestamp, reading): 2 groups x 130 hourly rows.
CREATE TABLE sensor_data AS
SELECT
    sensor_id,
    (TIMESTAMP '2020-01-01 00:00:00' + INTERVAL (i) HOUR) AS timestamp,
    20.0 + sensor_id * 2.0 + 3.0 * sin(2 * pi() * i / 24.0) AS reading
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sensor_id);

-- hourly_demand(region_id, timestamp, demand): 2 groups x 340 hourly rows (>=2x168),
-- daily (24) + weekly (168) seasonality.
CREATE TABLE hourly_demand AS
SELECT
    region_id,
    (TIMESTAMP '2020-01-01 00:00:00' + INTERVAL (i) HOUR) AS timestamp,
    100.0 + region_id * 10.0 + 10.0 * sin(2 * pi() * i / 24.0) + 5.0 * sin(2 * pi() * i / 168.0) AS demand
FROM range(0, 340) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region_id);

-- sub_daily_data(sensor_id, timestamp, reading): 2 groups x 340 hourly rows (>=2x168),
-- daily (24) + weekly (168) seasonality.
CREATE TABLE sub_daily_data AS
SELECT
    sensor_id,
    (TIMESTAMP '2020-01-01 00:00:00' + INTERVAL (i) HOUR) AS timestamp,
    50.0 + sensor_id * 5.0 + 8.0 * sin(2 * pi() * i / 24.0) + 4.0 * sin(2 * pi() * i / 168.0) AS reading
FROM range(0, 340) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sensor_id);

-- hourly_energy(meter_id, timestamp, consumption): 2 groups x 340 hourly rows (>=2x168),
-- daily (24) + weekly (168) seasonality.
CREATE TABLE hourly_energy AS
SELECT
    meter_id,
    (TIMESTAMP '2020-01-01 00:00:00' + INTERVAL (i) HOUR) AS timestamp,
    80.0 + meter_id * 8.0 + 15.0 * sin(2 * pi() * i / 24.0) + 6.0 * sin(2 * pi() * i / 168.0) AS consumption
FROM range(0, 340) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS meter_id);

-- monthly_sales(product_id, date, quantity): 2 groups x 36 monthly rows, trend.
CREATE TABLE monthly_sales AS
SELECT
    product_id,
    (DATE '2018-01-01' + INTERVAL (i) MONTH) AS date,
    100.0 + product_id * 10.0 + 1.5 * i + 8.0 * sin(2 * pi() * i / 12.0) AS quantity
FROM range(0, 36) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- quarterly_revenue(region_id, date, revenue): 2 groups x 130 daily rows, trend.
CREATE TABLE quarterly_revenue AS
SELECT
    region_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    400.0 + region_id * 25.0 + 0.5 * i + 10.0 * sin(2 * pi() * i / 7.0) AS revenue
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region_id);

-- raw(product_id, ds, y): 2 groups x 130 daily rows, trend + period-7 seasonality.
-- Generic "not yet cleaned" fixture name used by several SKILL.md walkthroughs.
CREATE TABLE raw AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
    65.0 + product_id * 6.5 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1 AS y
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- clean(product_id, ds, y): 2 groups x 130 daily rows, trend + period-7 seasonality.
-- Generic "already-cleaned" fixture name used by several SKILL.md walkthroughs.
CREATE TABLE clean AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
    60.0 + product_id * 6.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1 AS y
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- sales(product_id, date, value, revenue, quantity, amount, val): 2 groups x 130 daily rows,
-- trend + period-7 seasonality. Multiple value-column aliases because different doc sections
-- reference 'sales' with different target column names (pre-existing, out of scope).
CREATE TABLE sales AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS value,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) * 5.0 AS revenue,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS quantity,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS amount,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS val,
    (80.0 + product_id * 8.0 + 0.4 * i + 7.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.1) AS y
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- quarterly_data(region_id, date, revenue): 2 groups x 130 daily rows, trend.
CREATE TABLE quarterly_data AS
SELECT
    region_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    350.0 + region_id * 20.0 + 0.6 * i + 9.0 * sin(2 * pi() * i / 7.0) AS revenue
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region_id);

-- sales_with_weather(product_id, date, revenue, temperature): 2 groups x 130 daily rows,
-- historical source for the ts_forecast_exog_by walkthrough.
CREATE TABLE sales_with_weather AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    50.0 + product_id * 5.0 + 0.4 * i + 6.0 * sin(2 * pi() * i / 7.0) AS revenue,
    15.0 + 8.0 * sin(2 * pi() * i / 365.0) + (i % 5) * 0.2 AS temperature
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- weather_forecast(product_id, date, temperature): 2 groups x 20 future daily rows
-- (covers the 7-step forecast horizon used in the exog walkthrough).
CREATE TABLE weather_forecast AS
SELECT
    product_id,
    (DATE '2020-05-10' + INTERVAL (i) DAY) AS date,
    15.0 + 8.0 * sin(2 * pi() * (130 + i) / 365.0) + (i % 5) * 0.2 AS temperature
FROM range(0, 20) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- backtest_results(product_id, date, actual, forecast, point_forecast): 2 groups x 40
-- history rows (actual/forecast populated, point_forecast NULL) + 5 future rows
-- (point_forecast populated, actual/forecast NULL). Phase 16 plan 16-03: shared shape for
-- the conformal-prediction docs' "you already have backtest results" scenarios -- the
-- TABLE NAME varies by doc section (illustrative), the SHAPE does not.
CREATE TABLE backtest_results AS
SELECT product_id, date, actual, forecast, NULL::DOUBLE AS point_forecast,
       1.0 + (i % 5) * 0.5 AS recent_volatility
FROM (
    SELECT
        product_id, i,
        (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
        100.0 + product_id * 20.0 + i * 1.5 + 5.0 * sin(2 * pi() * i / 7.0) + (i % 5) * 0.3 AS actual,
        100.0 + product_id * 20.0 + i * 1.5 + (i % 5) * 0.2 AS forecast
    FROM range(0, 40) t(i)
    CROSS JOIN (SELECT unnest([1, 2]) AS product_id)
)
UNION ALL
SELECT product_id, date, NULL::DOUBLE, NULL::DOUBLE, point_forecast,
       1.0 + (i % 5) * 0.5 AS recent_volatility
FROM (
    SELECT
        product_id, i,
        (DATE '2024-02-10' + INTERVAL (i) DAY) AS date,
        100.0 + product_id * 20.0 + (40 + i) * 1.5 AS point_forecast
    FROM range(0, 5) t(i)
    CROSS JOIN (SELECT unnest([1, 2]) AS product_id)
);

-- Same shape, different illustrative names (Scenario 1 / Scenario 2 / Modular Approach
-- sections each narrate a different "assume you have this table" story).
CREATE TABLE my_backtest_results AS SELECT * FROM backtest_results;
CREATE TABLE historical_backtest AS SELECT * FROM backtest_results;

-- backtest(product_id, date, actual, forecast): the history-only rows (no point_forecast
-- column) for the Modular Approach's calibrate-then-apply walkthrough.
CREATE TABLE backtest AS
SELECT product_id, date, actual, forecast FROM backtest_results WHERE actual IS NOT NULL;

-- future_forecasts / todays_forecasts(product_id, date, forecast): the future-only rows,
-- renamed to a plain `forecast` column, for the "apply to any forecast table" examples.
CREATE TABLE future_forecasts AS
SELECT product_id, date, point_forecast AS forecast FROM backtest_results WHERE point_forecast IS NOT NULL;
CREATE TABLE todays_forecasts AS SELECT * FROM future_forecasts;

-- forecast_results(product_id, date, lower, upper): 2 groups x 5 rows, for the
-- ts_interval_width_by walkthrough.
CREATE TABLE forecast_results AS
SELECT
    product_id,
    date,
    point_forecast - 5.0 - product_id AS lower,
    point_forecast + 5.0 + product_id AS upper
FROM backtest_results WHERE point_forecast IS NOT NULL;

-- forecast_with_intervals: matches ts_conformal_apply_by's own output shape, so
-- "INSERT INTO forecast_with_intervals SELECT * FROM ts_conformal_apply_by(...)" type-checks.
CREATE TABLE forecast_with_intervals (
    group_col VARCHAR, date_col TIMESTAMP, point DOUBLE, lower DOUBLE, upper DOUBLE,
    method VARCHAR, conformity_score DOUBLE
);

-- backtest_data / forecast_table: same backtest_results shape, further illustrative names
-- used by examples/conformal_prediction/README.md.
CREATE TABLE backtest_data AS SELECT * FROM backtest_results;
CREATE TABLE forecast_table AS SELECT product_id, date, point_forecast FROM backtest_results WHERE point_forecast IS NOT NULL;

-- forecast_intervals(product_id, date, actual, lower_90, upper_90): 2 groups x 20 rows,
-- for examples/metrics/README.md's coverage/interval-width walkthrough.
CREATE TABLE forecast_intervals AS
SELECT
    product_id,
    (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
    100.0 + product_id * 10.0 + i * 0.5 AS actual,
    95.0 + product_id * 10.0 + i * 0.5 - (i % 3) AS lower_90,
    105.0 + product_id * 10.0 + i * 0.5 + (i % 3) AS upper_90
FROM range(0, 20) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- sales_data(product_id, ds, y): 'SKU_001' mean-reverting (stationary) + 'SKU_002'
-- random-walk (non-stationary), 50 daily rows each -- docs/api/10-diagnostics.md's
-- ts_adf / ts_adf_by examples (Phase 17 plan 17-02).
CREATE TABLE sales_data AS
SELECT 'SKU_001' AS product_id, (DATE '2023-01-01' + INTERVAL (i) DAY) AS ds,
       3.0 + 0.4 * sin(i * 0.6) + 0.15 * cos(i * 1.5) AS y
FROM range(0, 50) t(i)
UNION ALL
SELECT 'SKU_002' AS product_id, (DATE '2023-01-01' + INTERVAL (i) DAY) AS ds,
       sum(CASE WHEN i % 5 = 0 THEN -0.5 ELSE 0.2 END) OVER (ORDER BY i) AS y
FROM range(0, 50) t(i);

-- resids(series_id, ds, e): 'clean' (white-noise-like) + 'autocorr' (correlated),
-- 199 rows each -- docs/api/10-diagnostics.md's residual diagnostics examples and the
-- anofox-forecast-eda SKILL (Phase 17 plan 17-02).
CREATE TABLE resids AS
SELECT 'clean' AS series_id, i AS ds, (hash(i) % 1000 / 1000.0 - 0.5) AS e FROM range(1, 200) t(i)
UNION ALL
SELECT 'autocorr', i, sum(hash(j) % 1000 / 1000.0 - 0.5) OVER (ORDER BY i) AS e
FROM range(1, 200) t(i), LATERAL (SELECT i AS j);
