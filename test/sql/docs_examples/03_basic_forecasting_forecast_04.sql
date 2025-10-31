SELECT * FROM TS_FORECAST(
    'sales_complete',
    date,
    amount,
    'AutoETS',  -- Automatic model selection
    28,         -- 28 days ahead
    {'seasonal_period': 7}
);
