SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE category = 'Electronics'),
    product_id, date, amount, 'AutoETS', 28, {...}
);
