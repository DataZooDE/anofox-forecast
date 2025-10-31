-- Solution: Use non-seasonal model or get more data
SELECT * FROM TS_FORECAST('sales', date, amount, 'Naive', 28, MAP{});
