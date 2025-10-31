-- Compare confidence levels
SELECT 
    '90% CI' AS level,
    AVG(upper - lower) AS avg_width
FROM TS_FORECAST(..., {'confidence_level': 0.90, ...})
UNION ALL
SELECT 
    '95% CI',
    AVG(upper - lower)
FROM TS_FORECAST(..., {'confidence_level': 0.95, ...})
UNION ALL
SELECT 
    '99% CI',
    AVG(upper - lower)
FROM TS_FORECAST(..., {'confidence_level': 0.99, ...});
