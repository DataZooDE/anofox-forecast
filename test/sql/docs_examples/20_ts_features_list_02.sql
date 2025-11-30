-- List available features and their parameters
SELECT column_name, feature_name, default_parameters, parameter_keys
FROM anofox_fcst_ts_features_list()
ORDER BY column_name
LIMIT 10;

