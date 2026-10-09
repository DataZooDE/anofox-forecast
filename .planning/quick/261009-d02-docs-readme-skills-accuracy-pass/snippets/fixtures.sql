-- Deterministic synthetic fixtures for the docs/README/skills snippet harness
-- (quick task 261009-d02). Uses range()/sin()/modulo arithmetic only -- never random() --
-- so results are reproducible across runs. Grown iteratively as doc examples need new
-- table/column names; never papers over a real doc bug (unknown parameter, binder error,
-- wrong output column) with a fixture.

LOAD anofox_forecast;

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
