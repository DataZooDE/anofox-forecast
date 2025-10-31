-- Format for finance team
COPY (
    SELECT 
        DATE_TRUNC('month', date_col) AS month,
        product_id,
        SUM(point_forecast) AS revenue_forecast,
        SUM(lower) AS conservative_case,
        SUM(upper) AS optimistic_case,
        FIRST(model_name) AS model_used,
        FIRST(confidence_level) AS confidence_level
    FROM product_revenue_forecast
    GROUP BY month, product_id
    ORDER BY month, product_id
) TO 'revenue_forecast_Q4_2024.csv' (HEADER, DELIMITER ',');
