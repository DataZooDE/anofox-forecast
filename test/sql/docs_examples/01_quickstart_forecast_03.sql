-- Forecast next 14 days
SELECT * FROM TS_FORECAST(
    'my_sales',      -- table name
    date,            -- date column
    sales,           -- value column
    'AutoETS',       -- model (automatic)
    14,              -- horizon (14 days)
    {'seasonal_period': 7}  -- weekly seasonality
);
