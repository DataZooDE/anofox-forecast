-- Override default parameters for a feature
SELECT feats.*
FROM (
    SELECT ts_features(
        ts,
        value,
        ['ratio_beyond_r_sigma'],
        [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
    ) AS feats
    FROM sample_ts
);

