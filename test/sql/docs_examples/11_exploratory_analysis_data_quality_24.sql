-- Create a reusable preparation view
CREATE VIEW sales_autoprepared AS
WITH stats AS (
    SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount)
),
quality_series AS (
    SELECT series_id FROM stats WHERE quality_score >= 0.6
),
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
    WHERE product_id IN (SELECT series_id FROM quality_series)
),
no_constant AS (
    SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount)
),
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('no_constant', product_id, date, sales_amount)
)
SELECT * FROM complete;

-- Use in forecasting
SELECT * FROM TS_FORECAST_BY('sales_autoprepared', product_id, date, sales_amount,
                             'AutoETS', 28, {'seasonal_period': 7});
