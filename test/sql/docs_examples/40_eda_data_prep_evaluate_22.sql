-- Define your own quality criteria
WITH custom_quality AS (
    SELECT 
        series_id,
        quality_score,  -- Built-in
        -- Custom: Penalize intermittency
        CASE 
            WHEN intermittency > 0.5 THEN quality_score * 0.7
            ELSE quality_score
        END AS adjusted_quality,
        -- Custom: Require minimum length
        CASE 
            WHEN length < 60 THEN 0.0
            ELSE quality_score
        END AS length_adjusted_quality
    FROM sales_stats
)
SELECT 
    series_id,
    ROUND(quality_score, 4) AS original_quality,
    ROUND(adjusted_quality, 4) AS intermittency_adjusted,
    ROUND(length_adjusted_quality, 4) AS length_adjusted
FROM custom_quality
ORDER BY original_quality DESC;
