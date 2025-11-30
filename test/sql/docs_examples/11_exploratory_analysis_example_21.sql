-- Create sample e-commerce sales data
CREATE TABLE ecom_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Handle promotional spikes
CREATE TABLE ecommerce_prepared AS
WITH 
-- Identify promotion periods (changepoints)
changepoints AS (
    SELECT * FROM anofox_fcst_ts_detect_changepoints_by('ecom_sales', product_id, date, amount,
                                             MAP{'include_probabilities': true})
    WHERE changepoint_probability > 0.95  -- High confidence
),
-- Create regime indicator
with_regimes AS (
    SELECT 
        product_id,
        date_col AS date,
        value_col AS amount,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date_col) AS regime_id
    FROM changepoints
),
-- Compute regime statistics
regime_stats AS (
    SELECT 
        product_id,
        regime_id,
        AVG(amount) AS regime_avg
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
SELECT product_id, date, amount AS order_count, is_promo_period
FROM flagged;

-- Create table with non-promo data for forecasting
CREATE TABLE base_demand_data AS
SELECT product_id, date AS date_col, order_count AS value_col 
FROM ecommerce_prepared 
WHERE is_promo_period = false;

-- Forecast only on non-promo data for base demand
CREATE TABLE base_demand_forecast AS
SELECT * FROM anofox_fcst_ts_forecast_by(
    'base_demand_data',
    product_id, date_col, value_col,
    'AutoETS', 30, MAP{'seasonal_period': 7}
);
