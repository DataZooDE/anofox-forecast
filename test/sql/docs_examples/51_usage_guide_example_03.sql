FORECAST(
    timestamp_column VARCHAR,
    value_column VARCHAR,
    model VARCHAR,
    horizon INTEGER,
    model_params STRUCT
) → TABLE (
    forecast_step INTEGER,
    point_forecast DOUBLE,
    lower_95 DOUBLE,
    upper_95 DOUBLE,
    model_name VARCHAR,
    fit_time_ms DOUBLE
)
