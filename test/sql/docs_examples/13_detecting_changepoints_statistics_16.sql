WITH cp AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
),
segments AS (
    SELECT 
        *,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS segment_id
    FROM cp
)
SELECT 
    segment_id,
    COUNT(*) AS length,
    AVG(value_col) AS avg_value
FROM segments
GROUP BY segment_id;
