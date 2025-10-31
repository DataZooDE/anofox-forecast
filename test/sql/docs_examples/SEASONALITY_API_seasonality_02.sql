WITH aggregated AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM sales_data
)
SELECT TS_DETECT_SEASONALITY(values) AS periods
FROM aggregated;

-- Result: [7, 30]  (weekly and monthly seasonality)
