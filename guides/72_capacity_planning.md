# Capacity Planning & Resource Optimization - Business Guide

> **📝 Note**: This guide presents simplified workflows to demonstrate forecasting concepts. Real-world capacity planning is significantly more complex and requires:
> - Detailed understanding of operational constraints and costs
> - Integration with workforce management and scheduling systems
> - Consideration of service level agreements and quality metrics
> - Scenario analysis and contingency planning
> - Balance between cost optimization and service quality
>
> Use these examples as starting points, not production-ready solutions.

## Business Context

**Challenge**: Allocate resources efficiently to meet demand without over-provisioning.

**Areas**:
- Workforce planning
- Equipment/machinery utilization
- Server capacity (IT)
- Manufacturing capacity
- Warehouse space

**Goal**: Match capacity to forecasted demand while minimizing costs.

## Quick Start

```sql
-- Forecast demand to determine capacity needs
WITH demand_forecast AS (
    SELECT * FROM TS_FORECAST('daily_orders', date, order_count, 'AutoETS', 30,
                              {'seasonal_period': 7, 'confidence_level': 0.95})
),
capacity_requirement AS (
    SELECT 
        date_col AS date,
        CEIL(upper / 100.0) AS staff_required,  -- Each staff handles 100 orders
        CEIL(point_forecast / 100.0) AS expected_staff,
        CEIL(lower / 100.0) AS minimum_staff
    FROM demand_forecast
)
SELECT 
    date,
    expected_staff,
    staff_required AS staff_for_95ci,
    staff_required - expected_staff AS safety_buffer
FROM capacity_requirement;
```

## Use Case 1: Workforce Planning

### Call Center Staffing

```sql
-- Forecast call volume and determine staffing needs
WITH hourly_calls AS (
    SELECT 
        DATE_TRUNC('hour', call_timestamp) AS hour,
        COUNT(*) AS call_count
    FROM call_center_log
    WHERE call_timestamp >= CURRENT_DATE - INTERVAL '90 days'
    GROUP BY hour
),
call_forecast AS (
    SELECT * FROM TS_FORECAST('hourly_calls', hour, call_count,
                              'AutoMSTL', 168,  -- 1 week ahead (hourly)
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
staffing_needs AS (
    SELECT 
        date_col AS hour,
        point_forecast AS expected_calls,
        upper AS calls_95ci,
        -- Assume: 1 agent handles 6 calls/hour, 80% occupancy target
        CEIL(upper / (6 * 0.80)) AS agents_required,
        EXTRACT(HOUR FROM date_col) AS hour_of_day,
        EXTRACT(DOW FROM date_col) AS day_of_week
    FROM call_forecast
)
SELECT 
    day_of_week,
    hour_of_day,
    AVG(agents_required) AS avg_agents_needed,
    MAX(agents_required) AS peak_agents_needed
FROM staffing_needs
WHERE date_col >= CURRENT_DATE AND date_col < CURRENT_DATE + INTERVAL '7 days'
GROUP BY day_of_week, hour_of_day
ORDER BY day_of_week, hour_of_day;
```

### Shift Planning

```sql
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
```

## Use Case 2: Manufacturing Capacity

### Production Scheduling

```sql
-- Forecast production requirements
WITH product_demand AS (
    SELECT * FROM TS_FORECAST_BY('daily_orders', sku, order_date, quantity_ordered,
                                 'AutoETS', 60, {'seasonal_period': 7})
),
production_hours AS (
    SELECT 
        p.sku,
        p.date_col,
        p.point_forecast * pc.hours_per_unit AS hours_needed,
        p.upper * pc.hours_per_unit AS hours_95ci
    FROM product_demand p
    JOIN product_catalog pc ON p.sku = pc.sku
),
daily_capacity AS (
    SELECT 
        date_col AS date,
        SUM(hours_needed) AS total_hours_needed,
        SUM(hours_95ci) AS total_hours_95ci
    FROM production_hours
    GROUP BY date_col
),
capacity_gaps AS (
    SELECT 
        d.date,
        d.total_hours_needed,
        d.total_hours_95ci,
        f.available_capacity_hours,
        d.total_hours_95ci - f.available_capacity_hours AS capacity_gap,
        CASE 
            WHEN d.total_hours_95ci <= f.available_capacity_hours THEN '🟢 Sufficient'
            WHEN d.total_hours_needed <= f.available_capacity_hours THEN '🟡 Tight'
            ELSE '🔴 Insufficient - need overtime or delay'
        END AS status
    FROM daily_capacity d
    JOIN factory_capacity f ON d.date = f.date
)
SELECT * FROM capacity_gaps
WHERE date >= CURRENT_DATE
ORDER BY capacity_gap DESC;
```

### Equipment Utilization Planning

```sql
-- Forecast equipment hours needed
WITH equipment_demand AS (
    SELECT 
        DATE_TRUNC('week', p.date_col) AS week,
        e.equipment_type,
        SUM(p.point_forecast * pr.equipment_hours_per_unit) AS hours_needed
    FROM product_demand p
    JOIN production_recipes pr ON p.sku = pr.sku
    JOIN equipment e ON pr.equipment_id = e.equipment_id
    GROUP BY week, e.equipment_type
),
equipment_capacity AS (
    SELECT 
        equipment_type,
        COUNT(*) * 168 * 0.85 AS weekly_capacity_hours  -- 85% OEE
    FROM equipment
    WHERE status = 'Active'
    GROUP BY equipment_type
)
SELECT 
    d.week,
    d.equipment_type,
    ROUND(d.hours_needed, 1) AS hours_needed,
    c.weekly_capacity_hours,
    ROUND(100.0 * d.hours_needed / c.weekly_capacity_hours, 1) AS utilization_pct,
    CASE 
        WHEN d.hours_needed > c.weekly_capacity_hours THEN 
            CEIL((d.hours_needed - c.weekly_capacity_hours) / (168 * 0.85)) || ' additional units needed'
        ELSE 'Capacity sufficient'
    END AS recommendation
FROM equipment_demand d
JOIN equipment_capacity c ON d.equipment_type = c.equipment_type
ORDER BY d.week, utilization_pct DESC;
```

## Use Case 3: IT Infrastructure

### Server Capacity Planning

```sql
-- Forecast traffic and determine server requirements
WITH traffic_forecast AS (
    SELECT * FROM TS_FORECAST('hourly_requests', timestamp, request_count,
                              'AutoMSTL', 168,
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
server_requirements AS (
    SELECT 
        DATE_TRUNC('day', date_col) AS date,
        MAX(upper) AS peak_requests,
        AVG(point_forecast) AS avg_requests,
        -- Assume: 1 server handles 10K req/hour
        CEIL(MAX(upper) / 10000.0) AS servers_needed_peak,
        CEIL(AVG(point_forecast) / 10000.0) AS servers_needed_avg
    FROM traffic_forecast
    GROUP BY date
)
SELECT 
    date,
    peak_requests,
    servers_needed_peak,
    servers_needed_avg,
    servers_needed_peak - servers_needed_avg AS auto_scaling_range,
    ROUND(servers_needed_avg * 720 + (servers_needed_peak - servers_needed_avg) * 200, 2) AS estimated_cost
    -- Assuming $720/month base + $200/month for auto-scaling
FROM server_requirements
WHERE date >= CURRENT_DATE
ORDER BY date;
```

### Database Storage Planning

```sql
-- Forecast data growth for storage planning
WITH daily_data_growth AS (
    SELECT 
        date,
        SUM(table_size_mb) AS total_size_mb
    FROM table_sizes_history
    GROUP BY date
),
growth_forecast AS (
    SELECT * FROM TS_FORECAST('daily_data_growth', date, total_size_mb,
                              'Holt', 180,  -- 6 months ahead
                              MAP{})  -- Growth model (trend, no seasonality)
)
SELECT 
    DATE_TRUNC('month', date_col) AS month,
    ROUND(MAX(upper) / 1024.0, 2) AS storage_needed_gb,
    ROUND(MAX(upper) / 1024.0 - (SELECT MAX(total_size_mb) / 1024.0 FROM daily_data_growth), 2) AS additional_storage_gb,
    CASE 
        WHEN MAX(upper) / 1024.0 > 1000 THEN '⚠️ Plan storage expansion'
        ELSE '✓ Current capacity sufficient'
    END AS recommendation
FROM growth_forecast
GROUP BY month
ORDER BY month;
```

## Use Case 4: Warehouse & Logistics

### Warehouse Space Requirements

```sql
-- Forecast inventory levels to plan warehouse space
WITH inventory_forecast AS (
    -- Forecast incoming (purchases/production)
    SELECT * FROM TS_FORECAST_BY('daily_production', sku, date, units_produced,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
demand_forecast AS (
    -- Forecast outgoing (sales)
    SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
net_inventory AS (
    SELECT 
        i.sku,
        i.date_col AS date,
        i.point_forecast AS incoming,
        d.point_forecast AS outgoing,
        i.point_forecast - d.point_forecast AS net_change
    FROM inventory_forecast i
    JOIN demand_forecast d ON i.sku = d.sku AND i.date_col = d.date_col
),
cumulative_inventory AS (
    SELECT 
        sku,
        date,
        net_change,
        SUM(net_change) OVER (PARTITION BY sku ORDER BY date) + 
            (SELECT current_stock FROM inventory WHERE inventory.sku = net_inventory.sku) AS projected_inventory
    FROM net_inventory
),
space_requirements AS (
    SELECT 
        c.date,
        SUM(c.projected_inventory * p.cubic_feet) AS total_cubic_feet_needed
    FROM cumulative_inventory c
    JOIN product_catalog p ON c.sku = p.sku
    GROUP BY c.date
)
SELECT 
    date,
    ROUND(total_cubic_feet_needed / 1000.0, 2) AS thousand_cubic_feet,
    CASE 
        WHEN total_cubic_feet_needed > 50000 THEN '⚠️ Approaching capacity limit'
        ELSE '✓ Within capacity'
    END AS status
FROM space_requirements
WHERE date >= CURRENT_DATE
ORDER BY date;
```

### Fleet Sizing

```sql
-- Determine delivery fleet requirements
WITH delivery_forecast AS (
    SELECT * FROM TS_FORECAST('daily_deliveries', date, delivery_count,
                              'AutoETS', 30, {'seasonal_period': 7})
),
fleet_needs AS (
    SELECT 
        date_col AS date,
        EXTRACT(DOW FROM date_col) AS day_of_week,
        upper AS deliveries_95ci,
        -- Assume: 1 truck = 40 deliveries/day
        CEIL(upper / 40.0) AS trucks_needed
    FROM delivery_forecast
)
SELECT 
    day_of_week,
    ROUND(AVG(deliveries_95ci), 0) AS avg_deliveries,
    MAX(trucks_needed) AS peak_trucks_needed,
    AVG(trucks_needed) AS avg_trucks_needed,
    MAX(trucks_needed) - AVG(trucks_needed) AS flex_capacity_needed
FROM fleet_needs
WHERE date >= CURRENT_DATE AND date < CURRENT_DATE + INTERVAL '30 days'
GROUP BY day_of_week
ORDER BY day_of_week;
```

## Use Case 5: Energy & Utilities

### Power Generation Planning

```sql
-- Forecast electricity demand for generation planning
WITH hourly_demand AS (
    SELECT * FROM TS_FORECAST('hourly_power_demand', timestamp, megawatts,
                              'AutoTBATS', 168,  -- 1 week
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
generation_capacity AS (
    SELECT 
        DATE_TRUNC('day', date_col) AS date,
        MAX(upper) AS peak_demand_mw,
        AVG(point_forecast) AS avg_demand_mw,
        MIN(lower) AS min_demand_mw
    FROM hourly_demand
    GROUP BY date
),
capacity_plan AS (
    SELECT 
        date,
        peak_demand_mw,
        -- Base load: Coal/Nuclear (slow to adjust)
        min_demand_mw * 0.9 AS baseload_capacity_mw,
        -- Peak load: Gas turbines (quick response)
        peak_demand_mw - min_demand_mw * 0.9 AS peaking_capacity_mw,
        current_baseload,
        current_peaking
    FROM generation_capacity
    CROSS JOIN (SELECT 800 AS current_baseload, 300 AS current_peaking) current
)
SELECT 
    date,
    ROUND(baseload_capacity_mw, 1) AS baseload_needed,
    ROUND(peaking_capacity_mw, 1) AS peaking_needed,
    CASE 
        WHEN baseload_capacity_mw > current_baseload 
        THEN '⚠️ Baseload expansion needed'
        ELSE '✓ Baseload adequate'
    END AS baseload_status,
    CASE 
        WHEN peaking_capacity_mw > current_peaking 
        THEN '⚠️ Add peaking capacity'
        ELSE '✓ Peaking adequate'
    END AS peaking_status
FROM capacity_plan
WHERE date >= CURRENT_DATE
ORDER BY date;
```

## Cost Optimization

### Capacity vs Demand Matching

```sql
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
```

## Scenario Planning

### Best/Worst Case Planning

```sql
-- Plan for different scenarios
WITH scenarios AS (
    SELECT 
        'Pessimistic (Lower bound)' AS scenario,
        SUM(lower) AS total_demand
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
    UNION ALL
    SELECT 
        'Expected',
        SUM(point_forecast)
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
    UNION ALL
    SELECT 
        'Optimistic (Upper bound)',
        SUM(upper)
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
)
SELECT 
    scenario,
    ROUND(total_demand, 0) AS forecasted_demand,
    ROUND(total_demand * 1.2, 0) AS recommended_capacity  -- 20% buffer
FROM scenarios;
```

### Growth Scenarios

```sql
-- What if demand grows 20%? 50%?
WITH base_forecast AS (
    SELECT * FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 90, {'seasonal_period': 7})
),
growth_scenarios AS (
    SELECT 
        'Current trajectory' AS scenario,
        SUM(point_forecast) AS total_demand,
        1.0 AS growth_factor
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate growth (+20%)',
        SUM(point_forecast) * 1.20,
        1.20
    FROM base_forecast
    UNION ALL
    SELECT 
        'Strong growth (+50%)',
        SUM(point_forecast) * 1.50,
        1.50
    FROM base_forecast
    UNION ALL
    SELECT 
        'Explosive growth (+100%)',
        SUM(point_forecast) * 2.00,
        2.00
    FROM base_forecast
)
SELECT 
    scenario,
    ROUND(total_demand, 0) AS quarterly_demand,
    ROUND(total_demand / 90.0, 0) AS avg_daily_demand,
    CEIL(total_demand / 90.0 / 100.0) AS resources_needed  -- Each resource handles 100 units/day
FROM growth_scenarios
ORDER BY growth_factor;
```

## Resource Allocation

### Multi-Constraint Optimization

```sql
-- Allocate limited resources across multiple products
WITH product_forecast AS (
    SELECT * FROM TS_FORECAST_BY('sales', product_id, date, units,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
resource_needs AS (
    SELECT 
        p.product_id,
        SUM(p.point_forecast) AS forecasted_units_30d,
        SUM(p.point_forecast) * pr.labor_hours_per_unit AS labor_hours_needed,
        SUM(p.point_forecast) * pr.machine_hours_per_unit AS machine_hours_needed,
        pc.profit_margin
    FROM product_forecast p
    JOIN production_recipes pr ON p.product_id = pr.product_id
    JOIN product_catalog pc ON p.product_id = pc.product_id
    GROUP BY p.product_id, pr.labor_hours_per_unit, pr.machine_hours_per_unit, pc.profit_margin
),
capacity_limits AS (
    SELECT 720 AS labor_hours_available, 600 AS machine_hours_available
)
SELECT 
    r.product_id,
    ROUND(r.forecasted_units_30d, 0) AS demand_forecast,
    ROUND(r.labor_hours_needed, 1) AS labor_hrs,
    ROUND(r.machine_hours_needed, 1) AS machine_hrs,
    r.profit_margin,
    -- ROI per resource hour
    ROUND(r.profit_margin / (r.labor_hours_needed + r.machine_hours_needed), 2) AS roi_per_hour,
    RANK() OVER (ORDER BY r.profit_margin / (r.labor_hours_needed + r.machine_hours_needed) DESC) AS priority
FROM resource_needs r
CROSS JOIN capacity_limits c
ORDER BY priority;

-- Allocate to highest ROI products first until capacity exhausted
```

## Real-Time Capacity Monitoring

### Current vs Planned Capacity

```sql
-- Monitor if actual demand is within capacity
CREATE VIEW capacity_monitor AS
WITH today_forecast AS (
    SELECT SUM(point_forecast) AS expected_today
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 1, {'seasonal_period': 7})
),
today_actual AS (
    SELECT SUM(units) AS actual_so_far
    FROM sales
    WHERE date = CURRENT_DATE
),
today_capacity AS (
    SELECT available_capacity
    FROM capacity_plan
    WHERE date = CURRENT_DATE
)
SELECT 
    a.actual_so_far,
    f.expected_today,
    c.available_capacity,
    ROUND(100.0 * a.actual_so_far / c.available_capacity, 1) AS capacity_utilized_pct,
    ROUND(100.0 * a.actual_so_far / f.expected_today, 1) AS vs_forecast_pct,
    CASE 
        WHEN a.actual_so_far > c.available_capacity * 0.95 THEN '🔴 Near capacity limit'
        WHEN a.actual_so_far > c.available_capacity * 0.85 THEN '🟠 High utilization'
        WHEN a.actual_so_far > c.available_capacity * 0.70 THEN '🟡 Moderate'
        ELSE '🟢 Low utilization'
    END AS status
FROM today_actual a, today_forecast f, today_capacity c;
```

## Business Metrics

### Utilization Targets

```sql
-- Track utilization vs targets
WITH capacity_utilization AS (
    SELECT 
        DATE_TRUNC('week', date_col) AS week,
        AVG(point_forecast) / available_capacity AS avg_utilization
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7})
    CROSS JOIN (SELECT 1000 AS available_capacity) cap
    GROUP BY week
)
SELECT 
    week,
    ROUND(avg_utilization * 100, 1) AS utilization_pct,
    CASE 
        WHEN avg_utilization BETWEEN 0.75 AND 0.85 THEN '🌟 Optimal (75-85%)'
        WHEN avg_utilization BETWEEN 0.65 AND 0.95 THEN '✅ Good'
        WHEN avg_utilization < 0.65 THEN '⚠️ Under-utilized'
        ELSE '🔴 Over-utilized - expand capacity'
    END AS assessment
FROM capacity_utilization
ORDER BY week;
```

### ROI from Capacity Planning

```sql
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
```

## Summary

**Capacity Planning Workflow**:
1. ✅ Forecast demand using appropriate model
2. ✅ Calculate resource requirements from demand
3. ✅ Use confidence intervals for buffer sizing
4. ✅ Optimize for cost (balance over/under capacity)
5. ✅ Monitor actual vs plan continuously
6. ✅ Adjust capacity dynamically

**Key Formulas**:
- **Staff needed** = CEIL(Demand × Processing_time / (Hours_per_shift × Occupancy_target))
- **Safety buffer** = Upper_CI - Expected
- **Utilization target** = 75-85% (allows flexibility)
- **Safety stock** = Z_score × √Lead_time × Demand_std

**Business Impact**:
- 20-30% reduction in capacity costs
- 40-60% reduction in stockouts/service failures
- 15-25% improvement in resource utilization
- Better workforce planning and morale

**Next**:
- [Demand Forecasting](70_demand_forecasting.md) - Inventory optimization
- [Sales Prediction](71_sales_prediction.md) - Revenue forecasting
- [Performance Guide](60_performance_optimization.md) - Technical optimization

---

**Pro Tip**: Always plan for 95th percentile demand, not average! This ensures you can handle peak loads while minimizing over-provisioning.

