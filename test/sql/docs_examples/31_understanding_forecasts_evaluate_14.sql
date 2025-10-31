-- RMAE: Compare two models
SELECT 
    TS_RMAE(
        LIST(actual),
        LIST(autoets_forecast),
        LIST(naive_forecast)
    ) AS relative_mae;

-- < 1.0: AutoETS is better
-- = 1.0: Same performance
-- > 1.0: Naive is better
