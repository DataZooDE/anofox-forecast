-- Recommended: CrostonOptimized
SELECT * FROM TS_FORECAST_BY('spare_parts', part_number, date, demand,
                             'CrostonOptimized', 90, MAP{});

-- Alternative: ADIDA (if aggregation helps)
SELECT * FROM TS_FORECAST_BY('spare_parts', part_number, date, demand,
                             'ADIDA', 90, MAP{});
