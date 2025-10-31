TS_DETECT_CHANGEPOINTS(
    table_name: STRING,
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) -> TABLE(date_col TIMESTAMP, value_col DOUBLE, is_changepoint BOOLEAN)
