-- Segment time series into stable periods
WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('stock_prices', date, price, MAP{})
),
segments AS (
    SELECT 
        date_col,
        value_col,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS segment_id
    FROM changepoint_data
)
SELECT 
    segment_id,
    MIN(date_col) AS start_date,
    MAX(date_col) AS end_date,
    COUNT(*) AS days_in_segment,
    ROUND(AVG(value_col), 2) AS avg_value,
    ROUND(STDDEV(value_col), 2) AS volatility
FROM segments
GROUP BY segment_id
ORDER BY segment_id;
