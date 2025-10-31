TS_ANALYZE_SEASONALITY(
    timestamps: TIMESTAMP[], 
    values: DOUBLE[]
) -> STRUCT(
    detected_periods: INT[],
    primary_period: INT,
    seasonal_strength: DOUBLE,
    trend_strength: DOUBLE
)
