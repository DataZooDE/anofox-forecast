SELECT 
    TS_MASE(
        LIST(actual),
        LIST(forecast),
        LIST(naive_forecast)  -- Baseline
    ) AS mase;

-- < 1.0: Better than baseline ✅
-- = 1.0: Same as baseline
-- > 1.0: Worse than baseline ❌
