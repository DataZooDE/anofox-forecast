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
