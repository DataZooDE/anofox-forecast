SELECT TS_DETECT_SEASONALITY(LIST(sales ORDER BY date)) AS periods
FROM sales_data;
-- Returns: [7, 30] (weekly and monthly patterns)
