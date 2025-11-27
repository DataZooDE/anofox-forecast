# Demand Forecasting for Retail & Inventory - Business Guide

> **📝 Note**: This guide presents simplified workflows to demonstrate forecasting concepts. Real-world demand forecasting is significantly more complex and requires:
>
> - Domain expertise in retail/inventory management
> - Careful validation and testing
> - Business rules integration (promotions, seasonality, external factors)
> - Continuous monitoring and adjustment
> - Collaboration with business stakeholders
>
> Use these examples as starting points, not production-ready solutions.

## Business Problem

**Challenge**: Optimize inventory levels to:

- Minimize stockouts (lost sales)
- Minimize excess inventory (holding costs)
- Improve cash flow
- Enhance customer satisfaction

**Solution**: Accurate demand forecasting

## Business Value

### ROI Example

**Before Forecasting**:

- Stockout rate: 15%
- Excess inventory: 25%
- Lost sales: $500K/year
- Holding costs: $200K/year
- **Total cost**: $700K/year

**After Forecasting (85% accuracy)**:

- Stockout rate: 5%
- Excess inventory: 10%
- Lost sales: $150K/year
- Holding costs: $80K/year
- **Total cost**: $230K/year
- **Savings**: $470K/year (67% reduction)

## Quick Start for Business Users

### Step 1: Prepare Historical Sales Data

```sql
-- Your sales data should have:
-- 1. Product identifier
-- 2. Date
-- 3. Quantity sold

CREATE TABLE product_sales AS
SELECT 
    sku,
    sale_date,
    quantity_sold
FROM your_sales_table
WHERE sale_date >= CURRENT_DATE - INTERVAL '2 years';  -- 2 years history
```

### Step 2: Check Data Quality

```sql
-- Generate quality report
CREATE TABLE quality_stats AS
SELECT * FROM TS_STATS('product_sales', sku, sale_date, quantity_sold);

-- How many products have issues?
SELECT 
    COUNT(*) AS total_products,
    SUM(CASE WHEN quality_score < 0.7 THEN 1 ELSE 0 END) AS low_quality,
    ROUND(100.0 * SUM(CASE WHEN quality_score < 0.7 THEN 1 ELSE 0 END) / COUNT(*), 1) || '%' AS pct_low_quality
FROM quality_stats;

-- Identify problematic products
SELECT sku, quality_score, n_gaps, n_null
FROM quality_stats
WHERE quality_score < 0.7
ORDER BY quality_score
LIMIT 10;
```

### Step 3: Generate Forecasts

```sql
-- Forecast next 30 days for all products
CREATE TABLE demand_forecast AS
SELECT 
    sku,
    date_col AS forecast_date,
    ROUND(point_forecast, 0) AS forecasted_quantity,
    ROUND(lower, 0) AS min_quantity_95ci,
    ROUND(upper, 0) AS max_quantity_95ci,
    confidence_level
FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                    'AutoETS', 30,
                    {'seasonal_period': 7, 'confidence_level': 0.95});
```

### Step 4: Generate Reorder Recommendations

```sql
-- Calculate reorder quantities with safety stock
WITH daily_forecast AS (
    SELECT 
        sku,
        forecast_date,
        forecasted_quantity,
        max_quantity_95ci  -- Use upper bound for safety
    FROM demand_forecast
),
weekly_demand AS (
    SELECT 
        sku,
        DATE_TRUNC('week', forecast_date) AS week,
        SUM(forecasted_quantity) AS weekly_forecast,
        SUM(max_quantity_95ci) AS weekly_upper_bound
    FROM daily_forecast
    GROUP BY sku, week
),
inventory_current AS (
    SELECT sku, current_stock, lead_time_days
    FROM inventory
)
SELECT 
    w.sku,
    w.week,
    w.weekly_forecast AS expected_demand,
    w.weekly_upper_bound AS demand_95ci,
    i.current_stock,
    i.lead_time_days,
    GREATEST(0, w.weekly_upper_bound - i.current_stock) AS reorder_quantity,
    CASE 
        WHEN i.current_stock < w.weekly_forecast THEN '🔴 Reorder Now'
        WHEN i.current_stock < w.weekly_upper_bound THEN '🟡 Monitor'
        ELSE '🟢 OK'
    END AS status
FROM weekly_demand w
JOIN inventory_current i ON w.sku = i.sku
WHERE w.week = DATE_TRUNC('week', CURRENT_DATE)
ORDER BY status, reorder_quantity DESC;
```

## Advanced Use Cases

### Use Case 1: ABC Classification with Forecasts

```sql
-- Classify products by forecasted revenue
WITH forecast_revenue AS (
    SELECT 
        sku,
        SUM(forecasted_quantity * unit_price) AS forecasted_revenue_30d
    FROM demand_forecast df
    JOIN product_catalog pc ON df.sku = pc.sku
    GROUP BY sku
),
cumulative AS (
    SELECT 
        sku,
        forecasted_revenue_30d,
        SUM(forecasted_revenue_30d) OVER (ORDER BY forecasted_revenue_30d DESC) AS cumulative_revenue,
        SUM(forecasted_revenue_30d) OVER () AS total_revenue,
        ROW_NUMBER() OVER (ORDER BY forecasted_revenue_30d DESC) AS rank
    FROM forecast_revenue
)
SELECT 
    sku,
    ROUND(forecasted_revenue_30d, 2) AS revenue_30d,
    ROUND(100.0 * cumulative_revenue / total_revenue, 2) AS cumulative_pct,
    CASE 
        WHEN cumulative_revenue / total_revenue <= 0.80 THEN 'A - High Value'
        WHEN cumulative_revenue / total_revenue <= 0.95 THEN 'B - Medium Value'
        ELSE 'C - Low Value'
    END AS abc_class
FROM cumulative
ORDER BY rank;
```

### Use Case 2: Seasonal Inventory Planning

```sql
-- Identify seasonal products and plan accordingly
WITH seasonality AS (
    SELECT 
        sku,
        TS_DETECT_SEASONALITY(LIST(quantity_sold ORDER BY sale_date)) AS detected_periods
    FROM product_sales
    GROUP BY sku
),
seasonality_with_meta AS (
    SELECT 
        sku,
        detected_periods,
        CASE 
            WHEN LEN(detected_periods) > 0 THEN detected_periods[1]
            ELSE NULL 
        END AS primary_period,
        LEN(detected_periods) > 0 AS is_seasonal
    FROM seasonality
),
forecast_by_season AS (
    SELECT 
        df.sku,
        EXTRACT(MONTH FROM df.forecast_date) AS month,
        AVG(df.forecasted_quantity) AS avg_daily_demand,
        s.primary_period,
        s.is_seasonal
    FROM demand_forecast df
    JOIN seasonality_with_meta s ON df.sku = s.sku
    GROUP BY df.sku, month, s.primary_period, s.is_seasonal
)
SELECT 
    sku,
    month,
    ROUND(avg_daily_demand, 1) AS avg_demand,
    ROUND(avg_daily_demand * 30, 0) AS monthly_demand,
    CASE 
        WHEN is_seasonal THEN 'Adjust stock by season'
        ELSE 'Maintain steady stock'
    END AS inventory_strategy
FROM forecast_by_season
ORDER BY sku, month;
```

### Use Case 3: Safety Stock Optimization

```sql
-- Calculate optimal safety stock levels
WITH forecast_variance AS (
    SELECT 
        sku,
        AVG(forecasted_quantity) AS avg_demand,
        AVG(upper - lower) AS avg_uncertainty,
        STDDEV(forecasted_quantity) AS demand_volatility
    FROM demand_forecast
    GROUP BY sku
),
service_level AS (
    -- Service level factor (95% → z=1.645, 99% → z=2.326)
    SELECT 1.645 AS z_score  -- 95% service level
)
SELECT 
    f.sku,
    ROUND(f.avg_demand, 2) AS average_demand,
    ROUND(f.demand_volatility, 2) AS volatility,
    ROUND(s.z_score * f.demand_volatility * SQRT(i.lead_time_days), 0) AS safety_stock,
    ROUND(f.avg_demand * i.lead_time_days, 0) AS cycle_stock,
    ROUND(f.avg_demand * i.lead_time_days + s.z_score * f.demand_volatility * SQRT(i.lead_time_days), 0) AS reorder_point
FROM forecast_variance f
CROSS JOIN service_level s
JOIN inventory i ON f.sku = i.sku
ORDER BY safety_stock DESC;
```

### Use Case 4: Promotional Impact Analysis

```sql
-- Detect promotion periods using changepoints
WITH changepoints AS (
    SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('product_sales', sku, sale_date, quantity_sold,
                                             {'include_probabilities': true})
    WHERE is_changepoint = true
      AND changepoint_probability > 0.95  -- High confidence
),
promotion_periods AS (
    SELECT 
        sku,
        sale_date,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY sku ORDER BY sale_date) AS regime_id
    FROM TS_DETECT_CHANGEPOINTS_BY('product_sales', sku, sale_date, quantity_sold, MAP{})
),
regime_stats AS (
    SELECT 
        sku,
        regime_id,
        AVG(quantity_sold) AS avg_sales,
        COUNT(*) AS days_in_regime
    FROM promotion_periods
    GROUP BY sku, regime_id
)
SELECT 
    sku,
    regime_id,
    ROUND(avg_sales, 2) AS avg_daily_sales,
    days_in_regime,
    CASE 
        WHEN avg_sales > LAG(avg_sales) OVER (PARTITION BY sku ORDER BY regime_id) * 1.2
        THEN '🎯 Potential promotion period'
        ELSE 'Normal period'
    END AS interpretation
FROM regime_stats
ORDER BY sku, regime_id;
```

## Business Metrics & KPIs

### Forecast Accuracy to Business Impact

```sql
-- Translate forecast accuracy to business metrics
WITH accuracy AS (
    SELECT 
        sku,
        TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
        TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) AS coverage
    FROM validation_results
    GROUP BY sku
),
business_impact AS (
    SELECT 
        sku,
        ROUND(mape, 2) AS mape_pct,
        ROUND(coverage * 100, 1) AS coverage_pct,
        CASE 
            WHEN mape <= 10 THEN '🌟 Excellent (< 10% error)'
            WHEN mape <= 20 THEN '✅ Good (10-20% error)'
            WHEN mape <= 30 THEN '⚠️ Acceptable (20-30% error)'
            ELSE '❌ Poor (> 30% error)'
        END AS accuracy_grade,
        CASE 
            WHEN mape <= 10 THEN 'Optimize inventory further'
            WHEN mape <= 20 THEN 'Standard inventory policies work'
            WHEN mape <= 30 THEN 'Increase safety stock'
            ELSE 'Review forecasting approach'
        END AS recommendation
    FROM accuracy
)
SELECT * FROM business_impact ORDER BY mape_pct;
```

### ROI Calculator

```sql
-- Estimate ROI from improved forecasting
WITH current_state AS (
    SELECT 
        SUM(stockout_cost) AS total_stockout_cost,
        SUM(holding_cost) AS total_holding_cost
    FROM inventory_costs_last_year
),
forecast_state AS (
    SELECT 
        f.sku,
        -- Reduce stockouts by (1 - mape/100)
        i.stockout_cost * (a.mape / 100) AS new_stockout_cost,
        -- Reduce holding costs by optimizing inventory
        i.holding_cost * 0.6 AS new_holding_cost  -- Assume 40% reduction
    FROM forecasts f
    JOIN accuracy a ON f.sku = a.sku
    JOIN inventory_costs i ON f.sku = i.sku
),
savings AS (
    SELECT 
        c.total_stockout_cost AS current_stockout,
        c.total_holding_cost AS current_holding,
        SUM(f.new_stockout_cost) AS forecast_stockout,
        SUM(f.new_holding_cost) AS forecast_holding
    FROM current_state c
    CROSS JOIN forecast_state f
    GROUP BY c.total_stockout_cost, c.total_holding_cost
)
SELECT 
    ROUND(current_stockout, 0) AS current_stockout_cost,
    ROUND(forecast_stockout, 0) AS forecast_stockout_cost,
    ROUND(current_stockout - forecast_stockout, 0) AS stockout_savings,
    ROUND(current_holding, 0) AS current_holding_cost,
    ROUND(forecast_holding, 0) AS forecast_holding_cost,
    ROUND(current_holding - forecast_holding, 0) AS holding_savings,
    ROUND((current_stockout - forecast_stockout) + (current_holding - forecast_holding), 0) AS total_annual_savings
FROM savings;
```

## Operational Dashboards

### Dashboard 1: Daily Demand Overview

```sql
-- Today's demand forecast for all products
CREATE VIEW daily_demand_dashboard AS
SELECT 
    df.sku,
    pc.product_name,
    pc.category,
    df.forecasted_quantity AS tomorrow_forecast,
    df.min_quantity_95ci AS conservative_estimate,
    df.max_quantity_95ci AS optimistic_estimate,
    i.current_stock,
    CASE 
        WHEN i.current_stock >= df.max_quantity_95ci THEN '🟢 Sufficient'
        WHEN i.current_stock >= df.forecasted_quantity THEN '🟡 Adequate'
        WHEN i.current_stock >= df.min_quantity_95ci THEN '🟠 Low'
        ELSE '🔴 Critical - Reorder NOW'
    END AS stock_status,
    GREATEST(0, df.max_quantity_95ci - i.current_stock) AS suggested_reorder
FROM demand_forecast df
JOIN product_catalog pc ON df.sku = pc.sku
JOIN inventory i ON df.sku = i.sku
WHERE df.forecast_date = CURRENT_DATE + INTERVAL '1 day'
ORDER BY 
    CASE stock_status
        WHEN '🔴 Critical - Reorder NOW' THEN 1
        WHEN '🟠 Low' THEN 2
        WHEN '🟡 Adequate' THEN 3
        ELSE 4
    END,
    suggested_reorder DESC;
```

### Dashboard 2: Weekly Planning

```sql
-- Week-ahead forecast aggregated
CREATE VIEW weekly_demand_dashboard AS
WITH next_week AS (
    SELECT 
        sku,
        DATE_TRUNC('week', forecast_date) AS week,
        SUM(forecasted_quantity) AS weekly_demand,
        SUM(max_quantity_95ci) AS weekly_demand_upper
    FROM demand_forecast
    WHERE forecast_date BETWEEN CURRENT_DATE AND CURRENT_DATE + INTERVAL '7 days'
    GROUP BY sku, week
)
SELECT 
    n.sku,
    n.week,
    n.weekly_demand,
    n.weekly_demand_upper,
    i.current_stock,
    v.avg_unit_cost,
    ROUND(n.weekly_demand_upper * v.avg_unit_cost, 2) AS capital_requirement,
    CASE 
        WHEN i.current_stock < n.weekly_demand THEN 'Order required'
        ELSE 'Stock sufficient'
    END AS action
FROM next_week n
JOIN inventory i ON n.sku = i.sku
JOIN vendor_pricing v ON n.sku = v.sku
ORDER BY capital_requirement DESC;
```

### Dashboard 3: Forecast Accuracy Monitoring

```sql
-- Track forecast accuracy over time
CREATE VIEW forecast_accuracy_dashboard AS
WITH last_month_actuals AS (
    SELECT 
        sku,
        sale_date,
        quantity_sold AS actual
    FROM product_sales
    WHERE sale_date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
),
last_month_forecasts AS (
    SELECT 
        sku,
        forecast_date AS sale_date,
        forecasted_quantity AS forecast,
        min_quantity_95ci AS lower,
        max_quantity_95ci AS upper
    FROM forecast_history  -- Historical forecasts
    WHERE forecast_date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
)
SELECT 
    f.sku,
    COUNT(*) AS days_evaluated,
    ROUND(TS_MAE(LIST(a.actual), LIST(f.forecast)), 2) AS mae,
    ROUND(TS_MAPE(LIST(a.actual), LIST(f.forecast)), 2) AS mape_pct,
    ROUND(TS_COVERAGE(LIST(a.actual), LIST(f.lower), LIST(f.upper)) * 100, 1) AS coverage_pct,
    CASE 
        WHEN TS_MAPE(LIST(a.actual), LIST(f.forecast)) <= 15 THEN '🌟 Excellent'
        WHEN TS_MAPE(LIST(a.actual), LIST(f.forecast)) <= 25 THEN '✅ Good'
        ELSE '⚠️ Needs Improvement'
    END AS performance
FROM last_month_forecasts f
JOIN last_month_actuals a ON f.sku = a.sku AND f.sale_date = a.sale_date
GROUP BY f.sku
ORDER BY mape_pct;
```

## Real-World Scenarios

### Scenario 1: New Product Launch

```sql
-- For new products with limited history, use similar products
WITH similar_products AS (
    SELECT 
        new_sku,
        similar_sku
    FROM product_similarity
    WHERE new_sku = 'NEW_PRODUCT_001'
),
similar_forecast AS (
    SELECT 
        AVG(point_forecast) AS avg_forecast,
        forecast_step
    FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                        'AutoETS', 30, {'seasonal_period': 7})
    WHERE sku IN (SELECT similar_sku FROM similar_products)
    GROUP BY forecast_step
)
SELECT 
    'NEW_PRODUCT_001' AS sku,
    CURRENT_DATE + forecast_step AS forecast_date,
    ROUND(avg_forecast, 0) AS forecasted_quantity,
    '(Based on similar products)' AS note
FROM similar_forecast;
```

### Scenario 2: Promotion Planning

```sql
-- Forecast demand during promotion period
WITH base_forecast AS (
    SELECT * FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
promotion_lift AS (
    -- Historical promotion impact
    SELECT 
        sku,
        AVG(promotion_lift_factor) AS avg_lift
    FROM historical_promotions
    GROUP BY sku
)
SELECT 
    bf.sku,
    bf.forecast_date,
    ROUND(bf.forecasted_quantity, 0) AS base_demand,
    ROUND(bf.forecasted_quantity * (1 + pl.avg_lift), 0) AS promo_demand,
    ROUND(bf.forecasted_quantity * pl.avg_lift, 0) AS incremental_demand
FROM base_forecast bf
JOIN promotion_lift pl ON bf.sku = pl.sku
WHERE bf.forecast_date BETWEEN '2024-03-01' AND '2024-03-07'  -- Promotion week
ORDER BY bf.sku, bf.forecast_date;
```

### Scenario 3: Multi-Location Inventory

```sql
-- Forecast demand by location and aggregate
WITH location_forecast AS (
    SELECT * FROM TS_FORECAST_BY('sales_by_location', 
                                 location_id || '_' || sku AS series_key,
                                 sale_date, quantity_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
parsed AS (
    SELECT 
        SPLIT_PART(series_key, '_', 1) AS location_id,
        SPLIT_PART(series_key, '_', 2) AS sku,
        forecast_date,
        forecasted_quantity
    FROM location_forecast
)
SELECT 
    sku,
    forecast_date,
    SUM(forecasted_quantity) AS total_demand,
    MAX(forecasted_quantity) AS max_location_demand,
    FIRST(location_id) KEEP (DENSE_RANK FIRST ORDER BY forecasted_quantity DESC) AS top_location
FROM parsed
GROUP BY sku, forecast_date
ORDER BY sku, forecast_date;
```

## Business Alerts & Notifications

### Alert 1: Stockout Risk

```sql
-- Products at risk of stockout in next 7 days
CREATE VIEW stockout_alerts AS
WITH week_demand AS (
    SELECT 
        sku,
        SUM(max_quantity_95ci) AS week_demand_upper
    FROM demand_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '7 days'
    GROUP BY sku
)
SELECT 
    w.sku,
    i.current_stock,
    ROUND(w.week_demand_upper, 0) AS week_demand,
    ROUND(i.current_stock - w.week_demand_upper, 0) AS stock_deficit,
    CASE 
        WHEN i.current_stock < w.week_demand_upper * 0.5 THEN '🔴 URGENT'
        WHEN i.current_stock < w.week_demand_upper * 0.75 THEN '🟠 HIGH'
        ELSE '🟡 MEDIUM'
    END AS priority
FROM week_demand w
JOIN inventory i ON w.sku = i.sku
WHERE i.current_stock < w.week_demand_upper
ORDER BY priority, stock_deficit;
```

### Alert 2: Excess Inventory

```sql
-- Products with excess stock (> 60 days supply)
CREATE VIEW overstock_alerts AS
WITH monthly_forecast AS (
    SELECT 
        sku,
        AVG(forecasted_quantity) AS avg_daily_demand
    FROM demand_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY sku
)
SELECT 
    i.sku,
    i.current_stock,
    ROUND(f.avg_daily_demand, 2) AS avg_daily_demand,
    ROUND(i.current_stock / NULLIF(f.avg_daily_demand, 0), 0) AS days_of_supply,
    ROUND(i.current_stock * p.unit_cost, 2) AS capital_tied_up,
    CASE 
        WHEN i.current_stock / f.avg_daily_demand > 90 THEN '🔴 Critical overstock'
        WHEN i.current_stock / f.avg_daily_demand > 60 THEN '🟠 High overstock'
        ELSE '🟡 Consider clearance'
    END AS action
FROM inventory i
JOIN monthly_forecast f ON i.sku = f.sku
JOIN product_catalog p ON i.sku = p.sku
WHERE i.current_stock / NULLIF(f.avg_daily_demand, 0) > 60
ORDER BY days_of_supply DESC;
```

## Integration with BI Tools

### Export for Tableau/PowerBI

```sql
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
```

## Success Metrics

Track these KPIs to measure forecasting success:

### Inventory Metrics

- **Stockout Rate**: Target < 5%
- **Inventory Turnover**: Higher is better
- **Days of Supply**: 30-45 days optimal
- **Fill Rate**: Target > 95%

### Forecast Metrics

- **MAPE**: Target < 20%
- **Coverage**: Target 90-95% (matching CI level)
- **Bias**: Target near 0 (no systematic over/under forecast)

### Financial Metrics

- **Holding Cost Reduction**: Target 30-40%
- **Stockout Cost Reduction**: Target 50-70%
- **Cash Flow Improvement**: Track working capital
- **ROI**: Typical 5-10x in first year

## Summary

**Key Takeaways**:

- ✅ Demand forecasting directly impacts profitability
- ✅ Combine forecasts with inventory rules for optimal stock
- ✅ Monitor accuracy continuously
- ✅ Use confidence intervals for safety stock
- ✅ Segment products (ABC classification)
- ✅ Adjust for promotions and seasonality

**Next Steps**:

1. Implement basic forecasting pipeline
2. Set up monitoring dashboards
3. Integrate with procurement systems
4. Track ROI and iterate

---

**Related Guides**:

- [Sales Prediction](71_sales_prediction.md) - Revenue forecasting
- [Capacity Planning](72_capacity_planning.md) - Resource optimization
- [Anomaly Detection](33_anomaly_detection.md) - Outlier identification

**Need help?** Contact <support@anofox.com> for business consulting.
