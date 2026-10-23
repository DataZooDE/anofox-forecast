# Changepoint Detection

> Detect structural breaks in time series

## Overview

Changepoint detection identifies points in time where the statistical properties of a time series change significantly. The function returns row-level results, providing changepoint information for each data point in the input.

**Use this document to:**
- Detect structural breaks in mean, variance, or trend
- Find regime changes in sales, traffic, or sensor data
- Configure detection sensitivity with hazard rate parameter
- Get per-point changepoint probabilities for detailed analysis
- Identify series requiring model updates due to distribution shifts

**NULL handling (Phase 12, D-03/D-04):** the underlying `_ts_detect_changepoints_bocpd`
scalar shares the Rust single-series policy: leading/trailing `NULL` (and `NaN`) values
inside the `LIST(...)` are trimmed before the analysis runs; an interior `NULL` that
survives trimming is a hard error naming its 1-based position in the original list; a list
with no non-NULL value returns SQL `NULL`. The `is_changepoint`/`changepoint_probability`
arrays describe the **trimmed series** (position 1 = first non-NULL value) — the
single-series `ts_detect_changepoints` macro shifts its output back by the leading-NULL
row count so leading-NULL rows get `NULL` flags instead of misaligned ones.

```sql
-- Edge trim: leading/trailing NULLs give the same result as the trimmed series.
SELECT _ts_detect_changepoints_bocpd([NULL, 1.0, 2.0, 1.5, 8.0, 7.5, NULL]::DOUBLE[], 250.0, true)
     = _ts_detect_changepoints_bocpd([1.0, 2.0, 1.5, 8.0, 7.5]::DOUBLE[], 250.0, true);
-- true
```

---

## Algorithm (Phase 15 plan 15-03, DET-02, D-06)

`ts_detect_changepoints(_by)`/`ts_detect_changepoints_agg` (and the internal
`_ts_detect_changepoints_bocpd` scalar they all dispatch to) run Bayesian
Online Changepoint Detection (BOCPD, Adams & MacKay 2007) — a *different*
algorithm from PELT, which has no SQL surface at all (see the note at the
end of this section).

**Standardisation.** Before any detection runs, the series is standardised:
`sigma_hat` is a robust noise scale (the MAD of first differences, rescaled
to a Normal-equivalent sigma, with a fallback chain for a degenerate/
constant series), `loc = median(values)`, and `z = (values - loc) /
sigma_hat`. All run-length statistics below operate on `z`, not the raw
input — this is what makes the result scale- and shift-invariant:
`ts_detect_changepoints(x, ...)` and `ts_detect_changepoints(x*k+c, ...)`
flag identical changepoints for any `k != 0`.

**Model.** At each run length `r`, the Normal-Gamma conjugate prior is
`(mu0=0, kappa0=1, alpha0=1, beta0=1)` on `z`'s mean/precision. The
posterior update at each step uses the run's own SAMPLE mean (not the
posterior mean) for the sum-of-squares term — the textbook conjugate-update
formula. The predictive distribution for the next observation is a fully
normalised Student-t (the `Gamma((nu+1)/2) / (Gamma(nu/2) * sqrt(nu*pi) *
scale)` kernel, computed in the log domain for numerical stability), not an
unnormalised kernel. The hazard is a constant rate `1 / max(hazard_lambda,
1)` — `hazard_lambda` is the expected run length between changepoints.
Run-length state is capped at 500 entries: once a run length would exceed
that, the tail probability mass is merged into the last kept entry (not
dropped) and the distribution is renormalised, keeping both memory and the
total probability mass bounded on arbitrarily long series.

**Flag rule.** A changepoint is flagged when the run-length posterior's MAP
(most likely) run length *resets*: if `argmax_{r>=1} P(r_t) < argmax_{r>=1}
P(r_{t-1})`, the point `t - m_t + 1` is flagged — i.e. the flag lands at the
**start of the new, shorter run**, not at `t` itself. `changepoint_probability[t]`
is `P(r_t = 1)`: the probability that `x_t` itself opened a brand new run
(a different, complementary quantity from the flag rule above — a point can
have a low `P(r=1)` yet still be the flagged *start* of a run that was only
recognised as shorter a few steps later).

**Hazard guidance — the measured false-alarm/detection-delay tradeoff.**
Measured on 100 independent `N(0,1)` series of length 200 (no real
changepoint) and 100 independent 3-sigma-step series (step at index 100),
with the fixed implementation:

| `hazard_lambda` | False-alarm flags (total, over 100 noise series) | 3-sigma step detected within 3 samples |
|---|---|---|
| 250 (default) | 20 | 94/100 |
| 500 | 11 | 94/100 |
| 1000 | 5 | 94/100 |

A larger `hazard_lambda` (longer expected run length between changepoints)
trades fewer false alarms for the same detection delay over this range —
raising it does not cost detection latency on a clean 3-sigma step, only
reduces sensitivity to noise. This is measured behaviour, not a tuned-away
property: BOCPD will flag some fraction of stationary noise at any finite
`hazard_lambda`, by design (it is a continuously-updating online posterior,
not a fixed-false-alarm-rate test).

**PELT is Rust/FFI-only — not reachable from SQL.** `anofox_fcst_core::changepoint::detect_changepoints`
(PELT, Killick et al. 2012) is a separate, sigma-normalised algorithm with
working exact-safe pruning, verified against an independent numpy exact-DP
reference (`crates/anofox-fcst-core/tests/changepoint_reference.rs`). Its
SQL wrapper was removed as dead code in Phase 12, and `ts_detect_changepoints`
dispatches exclusively to BOCPD above — restoring a PELT SQL surface is a
deferred idea, not part of this phase.

---

## Quick Start

Detect changepoints using table macros:

```sql
-- Multiple series with row-level output
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    MAP{'hazard_lambda': '100'}
);

-- Filter to only changepoints
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    MAP{'hazard_lambda': '100'}
)
WHERE is_changepoint = true;
```

---

## Table Macros

### ts_detect_changepoints_by

Detect changepoints for each group in a table. Returns one row per data point with changepoint indicators.

**Signature:**
```sql
ts_detect_changepoints_by(source, group_col, date_col, value_col, params)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | MAP | Configuration options |

**Parameters in MAP** (Phase 20 plans 20-06/20-07, #287: `ts_detect_changepoints_by` and
`ts_detect_changepoints_agg` share one Rust decision for every key below, through a single
shared C++ params parser (`ts_changepoints_params.hpp`) and one Rust FFI export
(`anofox_ts_detect_changepoints_bocpd_opts`) — **both functions return identical rows for
identical inputs and params**, and every default below is unchanged from before Phase 20):
- `hazard_lambda`: Hazard rate parameter (default: `250.0`). Lower values detect more
  changepoints.
- `threshold`: Optional probability cutoff in `[0, 1]` (default: unset). When **unset**, the
  existing MAP run-length-reset heuristic decides `is_changepoint` (see Algorithm above). When
  **set**, `is_changepoint = (changepoint_probability >= threshold)` for every row **after the
  first** of each series — the first row of a series is **never** flagged, even at
  `threshold = 0`, because its probability is always `1 - 1/hazard_lambda` (the hazard prior
  alone, before any data is seen), not evidence of a changepoint. An out-of-range or
  non-numeric value throws, naming `threshold`.
- `deseasonalize`: Strict boolean (`true`/`false`/`1`/`0`, case-insensitive; default: `false`).
  When `true`, the series is transformed in Rust (`deseasonalize_for_changepoints`) **before**
  BOCPD runs: the STL seasonal component at `seasonal_period` (or the auto-detected period) is
  subtracted, and a straight-line (linear) trend is **always** removed afterwards — so a
  regular seasonal cycle stops producing changepoints, while a genuine level shift still
  survives as a step (a global line cannot absorb a local step). When `deseasonalize` is
  `true` and `threshold` is left unset, the **effective threshold defaults to `0.5`** (decided
  in Rust, so `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` both inherit it)
  instead of falling back to the MAP run-length-reset heuristic — removing the dominant
  seasonal variance collapses the BOCPD noise scale enough that the heuristic otherwise floods
  the series with spurious changepoints. An explicit `threshold` always wins over this
  default. **Opt-in only** — making `deseasonalize` itself the default is explicitly out of
  scope; defaults with `deseasonalize` unset/`false` are byte-identical to before this option
  existed (pinned by md5 in `test/sql/correctness/detection_decomposition.test`). An invalid
  (non-bool) value throws, naming `deseasonalize`.
- `seasonal_period`: Strict non-negative integer (default: `0` = auto-detect via
  `ts_detect_periods_by`'s FFT primary period). Only consulted when `deseasonalize` is `true`
  (accepted but ignored otherwise). A negative or non-numeric value throws, naming
  `seasonal_period`.

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier (column name preserved from input) |
| `<date_col>` | TIMESTAMP | Date/timestamp (column name preserved from input) |
| `is_changepoint` | BOOLEAN | Whether this point is a detected changepoint |
| `changepoint_probability` | DOUBLE | Probability of changepoint at this point (0-1) |

**Row Preservation:**

The function guarantees **output rows = input rows**. Rows that cannot be processed normally receive default values:

| Condition | is_changepoint | changepoint_probability |
|-----------|---------------|------------------------|
| NULL date | `false` | `NULL` |
| Group has < 2 data points | `false` | `NULL` |
| Processing error | `false` | `NULL` |

A warning is emitted when rows have NULL dates.

**Example:**
```sql
-- Detect changepoints with custom sensitivity
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    MAP{'hazard_lambda': '100'}
);

-- Find all changepoints with their dates
-- (output columns are group_col/date_col/is_changepoint/changepoint_probability only --
-- the value column is NOT included in ts_detect_changepoints_by's output)
SELECT
    product_id,
    date,
    changepoint_probability
FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    MAP{'hazard_lambda': '100'}
)
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Count changepoints per series
SELECT
    product_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS n_changepoints
FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    MAP{'hazard_lambda': '100'}
)
GROUP BY product_id;
```

> **Not an alias:** `ts_detect_changepoints` is a **different, single-series** macro, not an
> alias for `ts_detect_changepoints_by`. It takes 4 args — `(source, date_col, value_col,
> params)`, no `group_col` — and its output shape differs: `(date_col, value_col,
> is_changepoint, changepoint_probability)`. Notably it INCLUDES the value column (unlike
> `ts_detect_changepoints_by`, which omits it) and has no series identifier column at all.

---

## Aggregate Function

### ts_detect_changepoints_agg

Grouped aggregate form: one row per group, with a `LIST<STRUCT(...)>` of per-point results.
Returns **identical** `is_changepoint`/`changepoint_probability` values to
`ts_detect_changepoints_by` for identical inputs and params (Phase 20, #287) — pick whichever
shape fits the surrounding query.

**Signature:**
```sql
ts_detect_changepoints_agg(date_col, value_col, params) -- used with GROUP BY
```

**Parameters:** same `params` MAP keys as `ts_detect_changepoints_by` above
(`hazard_lambda`, `threshold`, `deseasonalize`, `seasonal_period`).

**Returns:** `LIST<STRUCT(timestamp TIMESTAMP, value DOUBLE, is_changepoint BOOLEAN, changepoint_probability DOUBLE)>` per group.

**Example:**
```sql
SELECT product_id, ts_detect_changepoints_agg(date, quantity, MAP{'hazard_lambda': '100'})
FROM sales
GROUP BY product_id;

-- Unnest to row-level output (equivalent shape to ts_detect_changepoints_by)
SELECT product_id, r.timestamp, r.is_changepoint, r.changepoint_probability
FROM (SELECT product_id, unnest(ts_detect_changepoints_agg(date, quantity, MAP{})) AS r FROM sales GROUP BY product_id);
```

---

## Ignoring Seasonal Cycles (deseasonalize, #287, D1-b)

A regular seasonal cycle (e.g. weekly traffic) can itself look like a sequence of
changepoints to BOCPD, drowning out a genuine level shift. `deseasonalize` removes the
seasonal component (and, always, a linear trend) before detection runs. When `threshold` is
left unset, the effective threshold defaults to `0.5` (code review CR-02) so only strong,
sustained shifts are flagged — the two calls below are equivalent:

```sql
CREATE TABLE weekly_with_shift AS
    SELECT 'series' AS id, (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
           40.0 + 0.3 * i + 5 * sin(2 * pi() * i / 7)
               + CASE WHEN i >= 50 THEN 15 ELSE 0 END
               + 1.6 * cos(i * 0.9) + 0.7 * sin(i * 1.7 + 40.0) AS y
    FROM range(84) t(i);

SELECT ds, is_changepoint
FROM ts_detect_changepoints_by('weekly_with_shift', id, ds, y,
    MAP{'deseasonalize': 'true'})
WHERE is_changepoint;
-- 2024-02-20   true    (the level shift -- weekly troughs are NOT flagged)

-- Equivalent -- an explicit threshold always wins, and 0.5 is the default
-- effective threshold whenever deseasonalize is true and threshold is unset:
SELECT ds, is_changepoint
FROM ts_detect_changepoints_by('weekly_with_shift', id, ds, y,
    MAP{'deseasonalize': 'true', 'threshold': '0.5'})
WHERE is_changepoint;
-- 2024-02-20   true    (identical result)
```

---

*See also: [Statistics](03-statistics.md) | [Feature Extraction](20-feature-extraction.md)*
