TS_DETECT_CHANGEPOINTS_BY(
    table_name: STRING,
    group_col: ANY,
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) -> TABLE(group_col ANY, date_col TIMESTAMP, value_col DOUBLE, is_changepoint BOOLEAN)
