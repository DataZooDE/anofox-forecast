SELECT * FROM TS_FORECAST_BY(
    'sales_complete',
    product_id,     -- Parallel forecasting per product
    date,
    amount,
    'AutoETS',
    28,
    {'seasonal_period': 7, 'confidence_level': 0.95}
);
