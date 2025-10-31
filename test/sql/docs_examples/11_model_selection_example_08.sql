-- Spare parts demand (many zeros)
SELECT * FROM TS_FORECAST('spare_parts', date, demand, 'CrostonOptimized', 28, MAP{});
