-- Optimize shift schedules based on forecasted demand
WITH hourly_demand AS (
    SELECT * FROM (previous call_forecast query)
),
shift_coverage AS (
    SELECT 
        DATE_TRUNC('day', date_col) AS day,
        CASE 
            WHEN EXTRACT(HOUR FROM date_col) BETWEEN 6 AND 14 THEN 'Morning'
            WHEN EXTRACT(HOUR FROM date_col) BETWEEN 14 AND 22 THEN 'Afternoon'
            ELSE 'Night'
        END AS shift,
        SUM(agents_required) AS total_agent_hours_needed
    FROM staffing_needs
    GROUP BY day, shift
)
SELECT 
    day,
    shift,
    CEIL(total_agent_hours_needed / 8.0) AS full_time_equivalents,
    total_agent_hours_needed AS agent_hours
FROM shift_coverage
ORDER BY day, 
    CASE shift 
        WHEN 'Morning' THEN 1 
        WHEN 'Afternoon' THEN 2 
        WHEN 'Night' THEN 3 
    END;
