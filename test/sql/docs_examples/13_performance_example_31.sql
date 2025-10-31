-- Parallel batch processing
CREATE TABLE forecasts_week_$(date +%Y%m%d) AS
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                             'AutoETS', 28, {'seasonal_period': 7});
