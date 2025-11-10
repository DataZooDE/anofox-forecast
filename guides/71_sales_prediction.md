# Sales Prediction & Revenue Forecasting - Business Guide

> **📝 Note**: This guide presents simplified workflows to demonstrate forecasting concepts. Real-world sales forecasting is significantly more complex and requires:
>
> - Deep understanding of sales cycles and market dynamics
> - Integration with CRM and sales pipeline data
> - Consideration of marketing campaigns, seasonality, and economic factors
> - Validation against actual business outcomes
> - Continuous model refinement based on performance
>
> Use these examples as starting points, not production-ready solutions.

## Business Context

**Objective**: Predict future sales and revenue for:

- Financial planning and budgeting
- Resource allocation
- Target setting
- Performance tracking
- Strategic decision-making

## Key Business Questions

1. **"What will our revenue be next quarter?"**
2. **"Which products will drive growth?"**
3. **"Are we on track to meet targets?"**
4. **"What's the risk range for our projections?"**

## Quick Start: Revenue Forecast

```sql
-- Load extension
LOAD 'anofox_forecast.duckdb_extension';

-- Forecast next quarter's revenue
WITH daily_forecast AS (
    SELECT * FROM TS_FORECAST('daily_revenue', date, revenue, 'AutoETS', 90,
                              {'seasonal_period': 7, 'confidence_level': 0.90})
),
quarterly_projection AS (
    SELECT 
        SUM(point_forecast) AS projected_revenue,
        SUM(lower) AS conservative_revenue,
        SUM(upper) AS optimistic_revenue
    FROM daily_forecast
)
SELECT 
    ROUND(projected_revenue, 0) AS q4_projection,
    ROUND(conservative_revenue, 0) AS worst_case_90ci,
    ROUND(optimistic_revenue, 0) AS best_case_90ci,
    ROUND(optimistic_revenue - conservative_revenue, 0) AS uncertainty_range
FROM quarterly_projection;
```

## Use Case 1: Product-Level Revenue Forecasting

### Data Preparation

```sql
-- Prepare sales data
CREATE TABLE product_sales_clean AS
WITH stats AS (
    SELECT * FROM TS_STATS('product_sales_raw', product_id, date, revenue)
),
-- Keep only products with sufficient history
good_quality AS (
    SELECT series_id FROM stats WHERE quality_score >= 0.7 AND length >= 90
),
-- Fill gaps
filled AS (
    SELECT * FROM TS_FILL_GAPS('product_sales_raw', product_id, date, revenue)
    WHERE product_id IN (SELECT series_id FROM good_quality)
),
-- Handle nulls
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('filled', product_id, date, revenue)
)
SELECT * FROM complete;
```

### Generate Forecasts

```sql
-- Forecast next 30 days per product
CREATE TABLE product_revenue_forecast AS
SELECT 
    product_id,
    date_col AS forecast_date,
    ROUND(point_forecast, 2) AS revenue_forecast,
    ROUND(lower, 2) AS revenue_lower_90ci,
    ROUND(upper, 2) AS revenue_upper_90ci,
    model_name,
    confidence_level
FROM TS_FORECAST_BY('product_sales_clean', product_id, date, revenue,
                    'AutoETS', 30,
                    {'seasonal_period': 7, 'confidence_level': 0.90});
```

### Business Analysis

```sql
-- Top revenue contributors
SELECT 
    product_id,
    SUM(revenue_forecast) AS total_30d_revenue,
    RANK() OVER (ORDER BY SUM(revenue_forecast) DESC) AS revenue_rank,
    ROUND(100.0 * SUM(revenue_forecast) / SUM(SUM(revenue_forecast)) OVER (), 2) AS pct_of_total
FROM product_revenue_forecast
GROUP BY product_id
ORDER BY revenue_rank
LIMIT 10;

-- Growth vs historical
WITH historical_30d AS (
    SELECT 
        product_id,
        SUM(revenue) AS historical_revenue
    FROM product_sales_clean
    WHERE date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
    GROUP BY product_id
),
forecasted_30d AS (
    SELECT 
        product_id,
        SUM(revenue_forecast) AS forecasted_revenue
    FROM product_revenue_forecast
    GROUP BY product_id
)
SELECT 
    f.product_id,
    ROUND(h.historical_revenue, 0) AS last_30d_actual,
    ROUND(f.forecasted_revenue, 0) AS next_30d_forecast,
    ROUND(100.0 * (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue, 1) AS growth_pct,
    CASE 
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > 0.10 THEN '📈 Strong growth'
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > 0 THEN '📊 Moderate growth'
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > -0.10 THEN '📉 Slight decline'
        ELSE '⚠️ Significant decline'
    END AS trend
FROM forecasted_30d f
JOIN historical_30d h ON f.product_id = h.product_id
ORDER BY growth_pct DESC;
```

## Use Case 2: Regional Sales Forecasting

```sql
-- Forecast by region for territory planning
WITH regional_sales AS (
    SELECT 
        r.region,
        s.date,
        SUM(s.revenue) AS regional_revenue
    FROM sales s
    JOIN store_locations sl ON s.store_id = sl.store_id
    JOIN regions r ON sl.region_id = r.region_id
    GROUP BY r.region, s.date
),
forecasts AS (
    SELECT * FROM TS_FORECAST_BY('regional_sales', region, date, regional_revenue,
                                 'AutoETS', 90,
                                 {'seasonal_period': 7, 'confidence_level': 0.95})
)
SELECT 
    region,
    DATE_TRUNC('month', date_col) AS month,
    SUM(point_forecast) AS monthly_revenue_forecast,
    SUM(upper) AS optimistic_scenario,
    SUM(lower) AS conservative_scenario
FROM forecasts
GROUP BY region, month
ORDER BY region, month;
```

## Use Case 3: New Product Revenue Projection

```sql
-- For new products: use similar product patterns
WITH new_products AS (
    SELECT product_id, category, launch_date
    FROM product_catalog
    WHERE launch_date > CURRENT_DATE - INTERVAL '30 days'
),
similar_product_curves AS (
    SELECT 
        pc.category,
        DATEDIFF('day', pc.launch_date, s.date) AS days_since_launch,
        AVG(s.revenue) AS avg_revenue_day_n
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    WHERE pc.launch_date IS NOT NULL
      AND DATEDIFF('day', pc.launch_date, s.date) BETWEEN 0 AND 90
    GROUP BY pc.category, days_since_launch
),
new_product_projection AS (
    SELECT 
        np.product_id,
        np.launch_date + INTERVAL (spc.days_since_launch) DAY AS projected_date,
        spc.avg_revenue_day_n AS projected_revenue
    FROM new_products np
    JOIN similar_product_curves spc ON np.category = spc.category
    WHERE spc.days_since_launch <= 90
)
SELECT 
    product_id,
    DATE_TRUNC('month', projected_date) AS month,
    SUM(projected_revenue) AS monthly_projection
FROM new_product_projection
GROUP BY product_id, month
ORDER BY product_id, month;
```

## Business Metrics

### Revenue at Risk

```sql
-- Calculate revenue at risk from forecast uncertainty
WITH uncertainty AS (
    SELECT 
        product_id,
        SUM(point_forecast) AS expected_revenue,
        SUM(lower) AS worst_case_revenue,
        SUM(upper) AS best_case_revenue
    FROM product_revenue_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY product_id
)
SELECT 
    product_id,
    ROUND(expected_revenue, 0) AS expected,
    ROUND(expected_revenue - worst_case_revenue, 0) AS downside_risk,
    ROUND(best_case_revenue - expected_revenue, 0) AS upside_potential,
    ROUND(100.0 * (worst_case_revenue / expected_revenue - 1), 1) || '%' AS downside_pct,
    ROUND(100.0 * (best_case_revenue / expected_revenue - 1), 1) || '%' AS upside_pct
FROM uncertainty
ORDER BY downside_risk DESC;
```

### Pipeline Health

```sql
-- Is your sales pipeline healthy?
WITH pipeline_forecast AS (
    SELECT 
        DATE_TRUNC('month', date_col) AS month,
        SUM(point_forecast) AS monthly_forecast
    FROM product_revenue_forecast
    GROUP BY month
),
targets AS (
    SELECT month, target_revenue
    FROM monthly_targets
)
SELECT 
    f.month,
    ROUND(f.monthly_forecast, 0) AS forecast,
    ROUND(t.target_revenue, 0) AS target,
    ROUND(f.monthly_forecast - t.target_revenue, 0) AS gap,
    ROUND(100.0 * f.monthly_forecast / t.target_revenue, 1) || '%' AS attainment_pct,
    CASE 
        WHEN f.monthly_forecast >= t.target_revenue * 1.05 THEN '🌟 Exceeding target'
        WHEN f.monthly_forecast >= t.target_revenue THEN '✅ On track'
        WHEN f.monthly_forecast >= t.target_revenue * 0.95 THEN '⚠️ Slightly behind'
        ELSE '🔴 Significant gap'
    END AS status
FROM pipeline_forecast f
JOIN targets t ON f.month = t.month
ORDER BY f.month;
```

## Scenario Planning

### Best/Worst/Most Likely

```sql
-- Three scenarios for board presentation
WITH scenarios AS (
    SELECT 
        'Most Likely (Expected)' AS scenario,
        SUM(point_forecast) AS revenue
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        'Pessimistic (Lower 90% CI)',
        SUM(lower)
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        'Optimistic (Upper 90% CI)',
        SUM(upper)
    FROM product_revenue_forecast
),
variance AS (
    SELECT 
        scenario,
        ROUND(revenue, 0) AS projected_revenue,
        ROUND(100.0 * revenue / (SELECT revenue FROM scenarios WHERE scenario LIKE 'Most%') - 100, 1) AS variance_pct
    FROM scenarios
)
SELECT * FROM variance
ORDER BY projected_revenue DESC;
```

### Sensitivity Analysis

```sql
-- How sensitive is revenue to forecast accuracy?
WITH accuracy_scenarios AS (
    SELECT 
        '100% accuracy' AS scenario,
        SUM(point_forecast) AS revenue,
        1.00 AS accuracy_factor
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '95% accuracy',
        SUM(point_forecast) * 0.95,
        0.95
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '90% accuracy',
        SUM(point_forecast) * 0.90,
        0.90
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '85% accuracy',
        SUM(point_forecast) * 0.85,
        0.85
    FROM product_revenue_forecast
)
SELECT 
    scenario,
    ROUND(revenue, 0) AS projected_revenue,
    ROUND(revenue - LAG(revenue) OVER (ORDER BY accuracy_factor DESC), 0) AS revenue_loss
FROM accuracy_scenarios
ORDER BY accuracy_factor DESC;
```

## Executive Dashboard

```sql
-- Monthly revenue projection dashboard
CREATE VIEW executive_revenue_dashboard AS
WITH monthly_forecast AS (
    SELECT 
        DATE_TRUNC('month', date_col) AS month,
        SUM(point_forecast) AS forecasted_revenue,
        SUM(lower) AS conservative_revenue,
        SUM(upper) AS optimistic_revenue
    FROM product_revenue_forecast
    GROUP BY month
),
historical_monthly AS (
    SELECT 
        DATE_TRUNC('month', date) AS month,
        SUM(revenue) AS actual_revenue
    FROM product_sales_clean
    GROUP BY month
),
combined AS (
    SELECT 
        COALESCE(f.month, h.month) AS month,
        h.actual_revenue,
        f.forecasted_revenue,
        f.conservative_revenue,
        f.optimistic_revenue,
        CASE WHEN h.actual_revenue IS NOT NULL THEN 'Actual' ELSE 'Forecast' END AS data_type
    FROM monthly_forecast f
    FULL OUTER JOIN historical_monthly h ON f.month = h.month
)
SELECT 
    month,
    data_type,
    ROUND(COALESCE(actual_revenue, forecasted_revenue), 0) AS revenue,
    ROUND(conservative_revenue, 0) AS lower_bound,
    ROUND(optimistic_revenue, 0) AS upper_bound,
    ROUND(100.0 * (revenue - LAG(revenue) OVER (ORDER BY month)) / LAG(revenue) OVER (ORDER BY month), 1) AS mom_growth_pct
FROM combined
ORDER BY month;
```

## Advanced Analytics

### Cohort Analysis

```sql
-- Revenue by customer acquisition cohort
WITH customer_cohorts AS (
    SELECT 
        customer_id,
        DATE_TRUNC('month', first_purchase_date) AS cohort_month
    FROM customers
),
cohort_revenue AS (
    SELECT 
        c.cohort_month,
        s.date,
        SUM(s.revenue) AS cohort_revenue
    FROM sales s
    JOIN customer_cohorts c ON s.customer_id = c.customer_id
    GROUP BY c.cohort_month, s.date
),
cohort_forecasts AS (
    SELECT * FROM TS_FORECAST_BY('cohort_revenue', cohort_month, date, cohort_revenue,
                                 'AutoETS', 90, {'seasonal_period': 7})
)
SELECT 
    cohort_month,
    SUM(point_forecast) AS q4_projected_revenue,
    RANK() OVER (ORDER BY SUM(point_forecast) DESC) AS cohort_rank
FROM cohort_forecasts
WHERE date_col BETWEEN '2024-01-01' AND '2024-03-31'
GROUP BY cohort_month
ORDER BY cohort_rank;
```

### Channel Attribution

```sql
-- Forecast revenue by sales channel
WITH channel_revenue AS (
    SELECT 
        channel,
        date,
        SUM(revenue) AS channel_revenue
    FROM sales
    GROUP BY channel, date
),
channel_forecasts AS (
    SELECT * FROM TS_FORECAST_BY('channel_revenue', channel, date, channel_revenue,
                                 'AutoETS', 30, {'seasonal_period': 7})
)
SELECT 
    channel,
    SUM(point_forecast) AS monthly_forecast,
    ROUND(100.0 * SUM(point_forecast) / SUM(SUM(point_forecast)) OVER (), 1) AS pct_of_total,
    CASE 
        WHEN RANK() OVER (ORDER BY SUM(point_forecast) DESC) <= 2
        THEN '🌟 Focus channel'
        ELSE 'Secondary'
    END AS strategic_importance
FROM channel_forecasts
WHERE date_col BETWEEN CURRENT_DATE AND CURRENT_DATE + INTERVAL '30 days'
GROUP BY channel
ORDER BY monthly_forecast DESC;
```

## Forecast vs Target Tracking

### Real-Time Performance Monitoring

```sql
-- Compare actual to forecast to target
CREATE VIEW performance_dashboard AS
WITH daily_actuals AS (
    SELECT 
        date,
        SUM(revenue) AS actual_revenue
    FROM sales
    WHERE date >= DATE_TRUNC('month', CURRENT_DATE)
    GROUP BY date
),
daily_forecasts AS (
    SELECT 
        date_col AS date,
        SUM(point_forecast) AS forecasted_revenue
    FROM product_revenue_forecast
    WHERE date_col >= DATE_TRUNC('month', CURRENT_DATE)
    GROUP BY date_col
),
daily_targets AS (
    SELECT 
        date,
        target_revenue
    FROM daily_revenue_targets
    WHERE date >= DATE_TRUNC('month', CURRENT_DATE)
)
SELECT 
    COALESCE(a.date, f.date, t.date) AS date,
    a.actual_revenue,
    f.forecasted_revenue,
    t.target_revenue,
    ROUND(100.0 * a.actual_revenue / NULLIF(f.forecasted_revenue, 0), 1) AS pct_of_forecast,
    ROUND(100.0 * a.actual_revenue / NULLIF(t.target_revenue, 0), 1) AS pct_of_target,
    CASE 
        WHEN a.actual_revenue >= t.target_revenue THEN '🌟 Beat target'
        WHEN a.actual_revenue >= f.forecasted_revenue THEN '✅ Above forecast'
        WHEN a.actual_revenue >= f.forecasted_revenue * 0.95 THEN '⚠️ Slightly below'
        ELSE '🔴 Underperforming'
    END AS status
FROM daily_actuals a
FULL OUTER JOIN daily_forecasts f ON a.date = f.date
FULL OUTER JOIN daily_targets t ON a.date = t.date
WHERE a.date IS NOT NULL OR f.date >= CURRENT_DATE
ORDER BY date;
```

### Month-to-Date Projections

```sql
-- Project end-of-month revenue based on current pace
WITH mtd_actual AS (
    SELECT SUM(revenue) AS mtd_revenue
    FROM sales
    WHERE date BETWEEN DATE_TRUNC('month', CURRENT_DATE) AND CURRENT_DATE
),
days_passed AS (
    SELECT DATE_DIFF('day', DATE_TRUNC('month', CURRENT_DATE), CURRENT_DATE) AS days
),
days_remaining AS (
    SELECT DATE_DIFF('day', CURRENT_DATE, DATE_TRUNC('month', CURRENT_DATE) + INTERVAL '1 month') AS days
),
remaining_forecast AS (
    SELECT SUM(point_forecast) AS remaining_revenue
    FROM product_revenue_forecast
    WHERE date_col BETWEEN CURRENT_DATE + INTERVAL '1 day' 
                       AND DATE_TRUNC('month', CURRENT_DATE) + INTERVAL '1 month'
)
SELECT 
    ROUND(mtd_revenue, 0) AS month_to_date,
    ROUND(remaining_revenue, 0) AS forecasted_remainder,
    ROUND(mtd_revenue + remaining_revenue, 0) AS projected_month_total,
    days AS days_passed,
    (SELECT days FROM days_remaining) AS days_left
FROM mtd_actual, remaining_forecast, days_passed;
```

## Risk Management

### Value at Risk (VaR)

```sql
-- 90% confidence: 10% chance revenue will be below this
WITH revenue_dist AS (
    SELECT 
        DATE_TRUNC('quarter', date_col) AS quarter,
        SUM(lower) AS var_10pct,  -- 10th percentile (for 90% CI lower bound)
        SUM(point_forecast) AS expected,
        SUM(upper) AS var_90pct   -- 90th percentile
    FROM product_revenue_forecast
    GROUP BY quarter
)
SELECT 
    quarter,
    ROUND(expected, 0) AS expected_revenue,
    ROUND(var_10pct, 0) AS revenue_at_risk_10pct,
    ROUND(expected - var_10pct, 0) AS potential_shortfall,
    ROUND(100.0 * (expected - var_10pct) / expected, 1) || '%' AS shortfall_pct
FROM revenue_dist
ORDER BY quarter;
```

### Stress Testing

```sql
-- What if sales drop 20%? What if they increase 30%?
WITH base_forecast AS (
    SELECT SUM(point_forecast) AS base_revenue
    FROM product_revenue_forecast
    WHERE date_col <= CURRENT_DATE + INTERVAL '30 days'
),
scenarios AS (
    SELECT 
        'Severe downturn (-30%)' AS scenario,
        base_revenue * 0.70 AS projected_revenue
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate downturn (-15%)',
        base_revenue * 0.85
    FROM base_forecast
    UNION ALL
    SELECT 
        'Base case',
        base_revenue
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate growth (+15%)',
        base_revenue * 1.15
    FROM base_forecast
    UNION ALL
    SELECT 
        'Strong growth (+30%)',
        base_revenue * 1.30
    FROM base_forecast
)
SELECT 
    scenario,
    ROUND(projected_revenue, 0) AS revenue,
    ROUND(projected_revenue - (SELECT base_revenue FROM base_forecast), 0) AS variance_from_base
FROM scenarios
ORDER BY projected_revenue;
```

## Actionable Insights

### Automated Recommendations

```sql
-- Generate business recommendations based on forecasts
CREATE VIEW revenue_recommendations AS
WITH forecast_analysis AS (
    SELECT 
        product_id,
        SUM(point_forecast) AS next_30d_revenue,
        AVG(point_forecast) AS avg_daily_revenue,
        STDDEV(point_forecast) AS revenue_volatility
    FROM product_revenue_forecast
    WHERE date_col <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY product_id
),
historical_avg AS (
    SELECT 
        product_id,
        AVG(revenue) AS hist_avg_daily
    FROM product_sales_clean
    WHERE date >= CURRENT_DATE - INTERVAL '90 days'
    GROUP BY product_id
),
analysis AS (
    SELECT 
        f.product_id,
        f.next_30d_revenue,
        f.avg_daily_revenue,
        h.hist_avg_daily,
        f.revenue_volatility,
        ROUND(100.0 * (f.avg_daily_revenue - h.hist_avg_daily) / h.hist_avg_daily, 1) AS growth_pct
    FROM forecast_analysis f
    JOIN historical_avg h ON f.product_id = h.product_id
)
SELECT 
    product_id,
    ROUND(next_30d_revenue, 0) AS next_30d_revenue,
    growth_pct || '%' AS growth_vs_hist,
    CASE 
        WHEN growth_pct > 20 THEN '🚀 Scale up: Increase inventory and marketing'
        WHEN growth_pct > 10 THEN '📈 Growing: Maintain current strategy'
        WHEN growth_pct > -10 THEN '↔️ Stable: Monitor closely'
        WHEN growth_pct > -20 THEN '📉 Declining: Investigate causes'
        ELSE '🔴 Trouble: Consider discontinuation or promotion'
    END AS recommendation
FROM analysis
ORDER BY growth_pct DESC;
```

## Integration with Business Systems

### Export for Financial Planning

```sql
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
```

### API for Real-Time Applications

```sql
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
```

## Summary

**Revenue Forecasting Best Practices**:

1. ✅ Prepare data carefully (30-50% accuracy improvement)
2. ✅ Use confidence intervals for risk management
3. ✅ Aggregate forecasts to business reporting periods (monthly, quarterly)
4. ✅ Track forecast vs actual continuously
5. ✅ Generate multiple scenarios (best/worst/expected)
6. ✅ Provide actionable recommendations
7. ✅ Integrate with existing business systems

**KPIs to Monitor**:

- Forecast accuracy (MAPE < 20% is good)
- Forecast bias (should be near 0)
- Interval coverage (should match confidence level)
- Revenue at risk (downside scenario)
- Attainment vs targets

**Next Steps**:

- [Capacity Planning](72_capacity_planning.md) - Resource optimization
- [Demand Forecasting](70_demand_forecasting.md) - Inventory management
- [Understanding Forecasts](31_understanding_forecasts.md) - Statistical concepts

---

**Business Impact**: Typical 15-25% improvement in revenue accuracy → Better planning, resource allocation, and strategic decisions!
