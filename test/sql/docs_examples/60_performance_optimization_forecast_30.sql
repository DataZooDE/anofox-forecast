-- Create parameterized prepared statement
PREPARE forecast_single AS
SELECT * FROM TS_FORECAST(
    (SELECT * FROM sales WHERE product_id = $1),
    date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- Execute with parameter
EXECUTE forecast_single('P12345');
