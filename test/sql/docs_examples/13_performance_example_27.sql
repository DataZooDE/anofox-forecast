-- Machine 1: Products A-M
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id < 'M'),
    product_id, date, amount, 'AutoETS', 28, {...}
);

-- Machine 2: Products N-Z
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id >= 'M'),
    product_id, date, amount, 'AutoETS', 28, {...}
);

-- Combine results
