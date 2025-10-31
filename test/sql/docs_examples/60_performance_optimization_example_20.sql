-- Only re-forecast series with new data
CREATE TABLE needs_update AS
SELECT product_id
FROM sales
WHERE date = CURRENT_DATE;

-- Forecast only updated series
CREATE TABLE new_forecasts AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM needs_update)),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Merge with existing forecasts
DELETE FROM forecasts WHERE product_id IN (SELECT product_id FROM needs_update);
INSERT INTO forecasts SELECT * FROM new_forecasts;
