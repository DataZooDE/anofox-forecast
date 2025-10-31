-- Create materialized view for BI dashboards
CREATE TABLE forecast_for_bi AS
SELECT 
    df.sku,
    pc.product_name,
    pc.category,
    pc.subcategory,
    df.forecast_date,
    df.forecasted_quantity,
    df.min_quantity_95ci AS lower_bound,
    df.max_quantity_95ci AS upper_bound,
    df.confidence_level,
    i.current_stock,
    i.lead_time_days,
    pc.unit_cost,
    ROUND(df.forecasted_quantity * pc.unit_price, 2) AS forecasted_revenue
FROM demand_forecast df
JOIN product_catalog pc ON df.sku = pc.sku
JOIN inventory i ON df.sku = i.sku;

-- Export to CSV for BI tools
COPY forecast_for_bi TO 'forecast_export.csv' (HEADER, DELIMITER ',');
