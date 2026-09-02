# Ensemble Member Introspection (`ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by`)

> Inspect which member models an ensemble selected and their combination weights, per series

## Overview

Two SQL functions expose the per-member weights and scores produced when fitting an ensemble.
Both return **long format** — one row per member per series — making it easy to pivot, rank,
filter, and join against other results.

| Function | When to use |
|----------|-------------|
| `ts_ensemble_inspect_by` | Explicit-member ensemble (`ts_forecast_ensemble_by`) — you name the models; inspect which weights were assigned |
| `ts_auto_ensemble_inspect_by` | AutoEnsemble (`ts_forecast_by(..., 'AutoEnsemble', ...)`) — inspect which candidates were selected and their in-sample MSE scores |

These functions require the same input table as the corresponding forecast call. They re-fit
the ensemble internally; they are not derived from a stored fit state.

---

## `ts_ensemble_inspect_by` — explicit-member weights

### Signature

```sql
ts_ensemble_inspect_by(
    source         VARCHAR,      -- source table (quoted string)
    group_col      IDENTIFIER,   -- series identifier (unquoted)
    date_col       IDENTIFIER,   -- date/timestamp column (unquoted)
    target_col     IDENTIFIER,   -- value column (unquoted)
    members        VARCHAR[],    -- 2+ model names, e.g. ['AutoARIMA','AutoETS','Theta']
    combination_method := '',    -- blend strategy; default '' = 'mean'
    seasonal_period := 0         -- shared period; 0 = non-seasonal
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE, score DOUBLE)
```

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `source` | VARCHAR | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | — | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | — | Target value column (unquoted) |
| `members` | VARCHAR[] | — | List of 2+ model names; same vocabulary as `ts_forecast_by` `method` param |
| `combination_method` | VARCHAR | `''` (= `'mean'`) | Blend strategy (see table below) |
| `seasonal_period` | INTEGER | `0` | Seasonal period shared by all members; `0` = non-seasonal |

### combination_method strings

| SQL string(s) | Combination strategy |
|---|---|
| `''` (empty), `'mean'` | Unweighted arithmetic mean — each member gets `1/k` |
| `'median'` | Coordinate-wise median — each member gets `1/k` (not weighted) |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weights; best-fitting members get higher weight |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weights; rewards parsimony |
| `'stacking'`, `'stack'` | Ridge-regularised stacking; simplex-projected |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-horizon weights; **`weight` column returns the AVERAGE across horizons** (per-step matrix not exposed — see Limitations) |

### Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `member_name` | VARCHAR | Model name (one row per member per series) |
| `weight` | DOUBLE | Combination weight; always non-negative; sums to 1.0 per group |
| `score` | DOUBLE | `NULL` — no per-member MSE score in explicit-member introspection |

### Weight properties

| combination_method | sum(weight) per group | weight values |
|---|---|---|
| `mean` / `median` / `''` | = 1.0 (within float tolerance) | All equal: `1/k` |
| `weighted_mse` | = 1.0 (by construction) | Non-negative; inverse-MSE proportional |
| `inverse_aic` | = 1.0 (by construction) | Non-negative; AIC-based |
| `stacking` | = 1.0 (simplex projection) | Non-negative; holdout-fitted |
| `horizon_adaptive` | = 1.0 (normalized average) | Per-step matrix averaged; use `ts_forecast_inspect_by` for per-step detail |

### SQL Examples (verified end-to-end)

```sql
-- Shared test series: 60-observation linear trend
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- Mean combination: all members get weight = 1/3
SELECT * FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, member_name;
-- Returns (for each member): weight = 0.333..., score = NULL
```

```sql
-- WeightedMSE combination: better-fitting members get higher weight
SELECT id, member_name, ROUND(weight, 6) AS weight, score
FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
ORDER BY id, member_name;
-- Returns: weight sums to 1.0 per group; best-fitting member gets highest weight
```

```sql
-- Assert weight sum = 1.0 per series (all combination methods)
SELECT id, SUM(weight) AS weight_sum
FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
GROUP BY id;
-- Returns: weight_sum = 1.0 for every series
```

---

## `ts_auto_ensemble_inspect_by` — AutoEnsemble member scores and optional weights

### Signature

```sql
ts_auto_ensemble_inspect_by(
    source         VARCHAR,      -- source table (quoted string)
    group_col      IDENTIFIER,   -- series identifier (unquoted)
    date_col       IDENTIFIER,   -- date/timestamp column (unquoted)
    target_col     IDENTIFIER,   -- value column (unquoted)
    top_k := 3,                  -- number of top candidates to select; default 3
    combination_method := '',    -- blend strategy; default '' = 'mean' (NOT 'weighted_mse')
    seasonal_period := 0         -- period for candidate models; 0 = non-seasonal
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE, score DOUBLE, rank BIGINT)
```

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `source` | VARCHAR | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | — | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | — | Target value column (unquoted) |
| `top_k` | INTEGER | `3` | Number of top-ranked candidates to select (same as `ts_forecast_by` AutoEnsemble param) |
| `combination_method` | VARCHAR | `''` (= `'mean'`) | Blend strategy. **`''` maps to `'mean'`, NOT `'weighted_mse'`** — pass `'weighted_mse'` explicitly for weighted combination |
| `seasonal_period` | INTEGER | `0` | Period passed to all candidate models; `0` = non-seasonal |

### Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `member_name` | VARCHAR | Selected candidate name (`AutoARIMA`, `AutoETS`, or `AutoTheta`) |
| `weight` | DOUBLE | `1/k` for `Mean` combination; `NULL` for all other methods (upstream crate 0.15.3 limitation — see below) |
| `score` | DOUBLE | In-sample MSE from `all_scores()`; lower is better; `>= 0` (near-zero for near-perfect fits) |
| `rank` | BIGINT | `1..k` ascending by MSE; rank 1 = best-fitting member (lowest MSE) |

### Weight availability (crate 0.15.3 limitation)

For `AutoEnsemble`, the inner `Ensemble` struct (which stores the combination weights for
`WeightedMSE` / `InverseAIC` / `Stacking` / `HorizonAdaptive`) is a **private field** of
`AutoEnsemble`. The `anofox-forecast` 0.15.3 public API exposes only `all_scores()`
(member name + in-sample MSE) and `model_count()` — not the inner weights.

| combination_method | `weight` column |
|---|---|
| `''` or `'mean'` | `1/k` (non-NULL) — trivially computable from `top_k` |
| `'weighted_mse'`, `'inverse_aic'`, `'stacking'`, `'horizon_adaptive'` | `NULL` — inner weights not accessible from crate 0.15.3 public API |

This limitation is tracked for a future crate release that exposes an inner-weight accessor.
The `score` (MSE) column is always available and useful for ranking regardless.

### Row count note

The returned row count per group may be **less than `top_k`** if fewer than `top_k`
candidate models fitted successfully (e.g., AutoARIMA convergence failure on a short or
irregular series). The actual count equals `model_count()` from the fitted AutoEnsemble.
Filter on `rank` to select the best k members:

```sql
-- Find the single best-fitting candidate per series
SELECT id, member_name, score
FROM ts_auto_ensemble_inspect_by('ae_test', id, ds, y)
WHERE rank = 1
ORDER BY id;
```

### SQL Examples (verified end-to-end)

```sql
-- AutoEnsemble Mean: weight = 1/3, score > 0, rank in (1,2,3)
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, rank;
-- Returns: 3 rows per series; weight=0.333 for all; rank=1 = lowest-MSE member
```

```sql
-- AutoEnsemble WeightedMSE: weight IS NULL (crate 0.15.3 limitation), score available
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'weighted_mse',  -- must be explicit: '' maps to 'mean'
    seasonal_period := 0
)
ORDER BY id, rank;
-- Returns: weight=NULL for all rows; score gives in-sample MSE ranking
```

```sql
-- Default call (combination_method := '' → 'mean'): weight = 1/k non-NULL
SELECT id, member_name, weight, ROUND(score, 8) AS score, rank
FROM ts_auto_ensemble_inspect_by('ae_test', id, ds, y)
ORDER BY id, rank;
```

---

## DoD Verification Queries

The following queries verify the weight consistency properties documented above.
All return 0 failures against the built extension (verified in `ensemble_inspect.sql`):

```sql
-- Shared data
CREATE OR REPLACE TABLE insp_series AS
SELECT
    CASE WHEN g = 0 THEN 'series_A' ELSE 'series_B' END AS id,
    (DATE '2023-01-01' + INTERVAL (r) DAY)::DATE AS ds,
    10.0 + r * 0.4 + sin(r * 0.2) * 3.0 + g * 5.0 AS y
FROM range(60) t(r), range(2) s(g);

-- 1. Explicit Mean: weights == 1/k and sum to 1
SELECT
    count(*) FILTER (WHERE abs(weight - 1.0/3.0) > 1e-10) AS unequal_mean_weights,
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-10) AS bad_weight_sum
FROM (
    SELECT id, weight, SUM(weight) OVER (PARTITION BY id) AS sum_w
    FROM ts_ensemble_inspect_by('insp_series', id, ds, y,
        ['AutoARIMA', 'AutoETS', 'Theta'],
        combination_method := 'mean', seasonal_period := 0)
);
-- Must return: unequal_mean_weights=0, bad_weight_sum=0

-- 2. Explicit WeightedMSE: non-negative, sum to 1
SELECT
    count(*) FILTER (WHERE weight < 0) AS negative_weights,
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-6) AS bad_weight_sum
FROM (
    SELECT id, weight, SUM(weight) OVER (PARTITION BY id) AS sum_w
    FROM ts_ensemble_inspect_by('insp_series', id, ds, y,
        ['AutoARIMA', 'AutoETS', 'Theta'],
        combination_method := 'weighted_mse', seasonal_period := 0)
);
-- Must return: negative_weights=0, bad_weight_sum=0

-- 3. AutoEnsemble Mean: weight=1/k, score>0, rank in 1..k
WITH m AS (SELECT * FROM ts_auto_ensemble_inspect_by('insp_series', id, ds, y, top_k := 3, combination_method := 'mean', seasonal_period := 0)),
     c AS (SELECT id, count(*) AS k FROM m GROUP BY id)
SELECT
    count(*) FILTER (WHERE m.weight IS NULL) AS null_weights,
    count(*) FILTER (WHERE abs(m.weight - 1.0/c.k) > 1e-10) AS unequal_weights,
    count(*) FILTER (WHERE m.score <= 0 OR m.score IS NULL) AS bad_scores
FROM m JOIN c USING (id);
-- Must return: null_weights=0, unequal_weights=0, bad_scores=0

-- 4. AutoEnsemble WeightedMSE: weight IS NULL, score > 0
SELECT
    count(*) FILTER (WHERE weight IS NOT NULL) AS non_null_weights,
    count(*) FILTER (WHERE score <= 0 OR score IS NULL) AS bad_scores
FROM ts_auto_ensemble_inspect_by('insp_series', id, ds, y, top_k := 3, combination_method := 'weighted_mse', seasonal_period := 0);
-- Must return: non_null_weights=0, bad_scores=0
```

---

## Limitations

### `score` column is `NULL` for explicit-member ensembles

`ts_ensemble_inspect_by` does not return a per-member MSE score. The `score` column is
always `NULL`. This is by design — the explicit-member ensemble does not expose per-member
in-sample MSE in the current crate 0.15.3 API. Use `ts_auto_ensemble_inspect_by` if you
need scored member ranking.

### AutoEnsemble combination weights are `NULL` for non-Mean methods (crate 0.15.3)

For `AutoEnsemble` with `combination_method` other than `'mean'`, the inner combination
weights (`WeightedMSE` / `InverseAIC` / `Stacking` / `HorizonAdaptive`) are not accessible
from the `anofox-forecast` 0.15.3 public API. `weight` is `NULL` for these rows. The `score`
(in-sample MSE) is always available. This limitation is tracked upstream as a future
enhancement — a public inner-weight accessor would enable full weight transparency.

### `HorizonAdaptive` returns average weights, not per-step weights

For `combination_method := 'horizon_adaptive'`, `Ensemble::weights()` returns the AVERAGE
of per-horizon weight vectors, not the full per-step matrix. The per-step matrix is in
`horizon_weights()` (a `Vec<Vec<f64>>`), but representing it in the long-format
`(group, member, weight)` schema is non-trivial and is out of scope for INSP-01.

### `''` maps to `'mean'`, not `'weighted_mse'`

The empty string `combination_method := ''` maps to `CombinationMethod::Mean` via
`parse_combination_method` — **not** to `WeightedMSE`. This is consistent with
`ts_forecast_ensemble_by` and `ts_forecast_by` defaults (both default to Mean). To get
`WeightedMSE` behavior, pass `combination_method := 'weighted_mse'` explicitly.

---

## Reference

- `examples/forecasting/ensemble_inspect.sql` — full INSP-01 DoD example (4 sections, all assertions 0 failures)
- `examples/forecasting/ensemble_intervals.sql` — EPI-01 conformal interval example for both ensemble surfaces
- [ensemble_explicit reference](ensemble_explicit.md) — `ts_forecast_ensemble_by` full docs
- [autoensemble reference](autoensemble.md) — `AutoEnsemble` full parameter docs
- [11-conformal-prediction.md](../../../api/11-conformal-prediction.md) — ensemble conformal interval paths (EPI-01)
- [07-forecasting.md](../../../api/07-forecasting.md) — top-level forecasting API reference
