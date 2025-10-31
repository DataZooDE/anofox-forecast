STRUCT {
    forecast_step: LIST<INT>,
    forecast_timestamp: LIST<TIMESTAMP>,
    point_forecast: LIST<DOUBLE>,
    lower: LIST<DOUBLE>,
    upper: LIST<DOUBLE>,
    model_name: VARCHAR,
    insample_fitted: LIST<DOUBLE>,
    confidence_level: DOUBLE
}
