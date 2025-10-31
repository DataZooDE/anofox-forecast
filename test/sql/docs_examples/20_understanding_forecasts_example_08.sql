SELECT 
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results;

-- For 95% CI, expect ~95% coverage
