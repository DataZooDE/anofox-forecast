-- Process in batches
CREATE TABLE forecasts_batch1 AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id BETWEEN 'P000' AND 'P999'),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

CREATE TABLE forecasts_batch2 AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id BETWEEN 'P1000' AND 'P1999'),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Combine
CREATE TABLE all_forecasts AS
SELECT * FROM forecasts_batch1
UNION ALL
SELECT * FROM forecasts_batch2;
