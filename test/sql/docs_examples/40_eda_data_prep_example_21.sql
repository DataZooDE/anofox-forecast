-- Handle promotional spikes
CREATE TABLE ecommerce_prepared AS
WITH 
-- Identify promotion periods (changepoints)
changepoints AS (
    SELECT * FROM TS_DETECT_CHANGEPOINTS_BY('ecom_sales', product_id, date, order_count,
                                             {'include_probabilities': true})
    WHERE changepoint_probability > 0.95  -- High confidence
),
-- Create regime indicator
with_regimes AS (
    SELECT 
        product_id,
        date,
        order_count,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS regime_id
    FROM changepoints
),
-- Compute regime statistics
regime_stats AS (
    SELECT 
        product_id,
        regime_id,
        AVG(order_count) AS regime_avg
    FROM with_regimes
    GROUP BY product_id, regime_id
),
-- Flag promotion periods
flagged AS (
    SELECT 
        w.*,
        CASE 
            WHEN rs.regime_avg > 
                 LAG(rs.regime_avg) OVER (PARTITION BY w.product_id ORDER BY rs.regime_id) * 1.3
            THEN true
            ELSE false
        END AS is_promo_period
    FROM with_regimes w
    JOIN regime_stats rs ON w.product_id = rs.product_id AND w.regime_id = rs.regime_id
)
SELECT product_id, date, order_count, is_promo_period
FROM flagged;

-- Forecast only on non-promo data for base demand
CREATE TABLE base_demand_forecast AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM ecommerce_prepared WHERE is_promo_period = false),
    product_id, date, order_count,
    'AutoETS', 30, {'seasonal_period': 7}
);
