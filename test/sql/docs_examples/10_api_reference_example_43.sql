-- Good: Single TS_FORECAST_BY call
SELECT * FROM TS_FORECAST_BY('sales', product_id, ...);

-- Avoid: Multiple individual calls
