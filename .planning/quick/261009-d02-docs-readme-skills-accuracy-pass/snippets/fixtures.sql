-- Deterministic synthetic fixtures for the docs/README/skills snippet harness
-- (quick task 261009-d02). Uses range()/sin()/modulo arithmetic only -- never random() --
-- so results are reproducible across runs. Grown iteratively as doc examples need new
-- table/column names; never papers over a real doc bug (unknown parameter, binder error,
-- wrong output column) with a fixture.

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
