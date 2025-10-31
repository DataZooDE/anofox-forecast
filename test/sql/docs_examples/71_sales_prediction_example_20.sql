-- Create view for API consumption
CREATE VIEW api_revenue_forecast AS
SELECT 
    product_id,
    date_col AS forecast_date,
    point_forecast AS expected_revenue,
    lower AS min_revenue_90ci,
    upper AS max_revenue_90ci,
    confidence_level,
    model_name,
    CURRENT_TIMESTAMP AS forecast_generated_at
FROM product_revenue_forecast;

-- Access via DuckDB API or export
SELECT * FROM api_revenue_forecast
WHERE forecast_date = CURRENT_DATE + INTERVAL '1 day';
