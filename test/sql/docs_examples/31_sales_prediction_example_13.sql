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
