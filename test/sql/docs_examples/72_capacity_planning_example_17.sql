-- Calculate ROI from accurate capacity forecasts
WITH capacity_costs AS (
    SELECT 
        'Excess capacity' AS cost_type,
        SUM((planned_capacity - actual_demand) * cost_per_unit) AS cost
    FROM capacity_history
    WHERE planned_capacity > actual_demand
    UNION ALL
    SELECT 
        'Insufficient capacity',
        SUM((actual_demand - planned_capacity) * lost_revenue_per_unit)
    FROM capacity_history
    WHERE planned_capacity < actual_demand
),
forecast_accuracy AS (
    SELECT AVG(1 - TS_MAPE(LIST(actual), LIST(forecast)) / 100.0) AS accuracy_rate
    FROM validation_results
)
SELECT 
    cost_type,
    ROUND(cost, 0) AS annual_cost,
    ROUND(cost * (SELECT accuracy_rate FROM forecast_accuracy), 0) AS estimated_savings
FROM capacity_costs;
