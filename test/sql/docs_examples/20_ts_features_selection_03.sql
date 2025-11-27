-- Select specific features for better performance
SELECT feats.*
FROM (
    SELECT ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
    FROM sample_ts
);

