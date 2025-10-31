-- Calculate optimal capacity level
WITH demand_distribution AS (
    SELECT 
        date_col,
        point_forecast AS expected_demand,
        lower AS demand_5pct,
        upper AS demand_95pct
    FROM TS_FORECAST('daily_demand', date, demand_units, 'AutoETS', 90, 
                     {'seasonal_period': 7, 'confidence_level': 0.90})
),
capacity_scenarios AS (
    SELECT 
        'Match expected (50%)' AS scenario,
        AVG(expected_demand) AS capacity_level,
        SUM(CASE WHEN expected_demand > AVG(expected_demand) OVER () THEN expected_demand - AVG(expected_demand) OVER () ELSE 0 END) AS unmet_demand,
        0 AS excess_capacity
    FROM demand_distribution
    UNION ALL
    SELECT 
        'Match 75th percentile',
        QUANTILE_CONT(expected_demand, 0.75) OVER (),
        SUM(CASE WHEN expected_demand > QUANTILE_CONT(expected_demand, 0.75) OVER () THEN expected_demand - QUANTILE_CONT(expected_demand, 0.75) OVER () ELSE 0 END),
        SUM(CASE WHEN expected_demand < QUANTILE_CONT(expected_demand, 0.75) OVER () THEN QUANTILE_CONT(expected_demand, 0.75) OVER () - expected_demand ELSE 0 END)
    FROM demand_distribution
    UNION ALL
    SELECT 
        'Match 95th percentile (95% CI upper)',
        AVG(demand_95pct),
        SUM(CASE WHEN demand_95pct > AVG(demand_95pct) OVER () THEN demand_95pct - AVG(demand_95pct) OVER () ELSE 0 END),
        SUM(CASE WHEN expected_demand < AVG(demand_95pct) OVER () THEN AVG(demand_95pct) OVER () - expected_demand ELSE 0 END)
    FROM demand_distribution
)
SELECT 
    scenario,
    ROUND(capacity_level, 0) AS capacity_units,
    ROUND(unmet_demand, 0) AS total_unmet_demand,
    ROUND(excess_capacity, 0) AS total_excess_capacity,
    ROUND(unmet_demand * 50 + excess_capacity * 10, 0) AS estimated_cost
    -- Assume: $50/unit unmet demand cost, $10/unit excess capacity cost
FROM capacity_scenarios;

-- Choose scenario that minimizes total cost
