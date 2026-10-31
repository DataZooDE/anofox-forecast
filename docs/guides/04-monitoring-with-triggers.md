# Monitoring Forecasts with Triggers

> In-database forecast monitoring on DuckDB 2.0: alerts and forecast refreshes on INSERT

**Use this guide to:**
- Raise an alert row when a new actual falls outside its forecast interval
- Keep a forecasts table current as new actuals arrive, refitting only the series that changed
- Choose between a trigger and a scheduled query

**Requires DuckDB 2.0+** (row triggers, statement triggers with `MERGE INTO`). On v1.5.6
run the same SQL as a scheduled query (cron, an orchestrator), see
[Without triggers](#without-triggers-duckdb-156) below. Both recipes are tested in
`test/sql/ts_triggers.test`.

## Recipe 1: interval-breach alerts

A row trigger compares every inserted actual with its stored forecast interval and records a
breach. The lookup is a single-row join, so it is cheap enough to run per row.

```sql
-- DuckDB 2.0+
CREATE TABLE forecasts (id VARCHAR, ds TIMESTAMP, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE);
INSERT INTO forecasts VALUES ('a', TIMESTAMP '2024-02-01', 10, 8, 12),
                             ('a', TIMESTAMP '2024-02-02', 10, 8, 12);

CREATE TABLE actuals (id VARCHAR, ds TIMESTAMP, y DOUBLE);
CREATE TABLE alerts  (id VARCHAR, ds TIMESTAMP, y DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE);

CREATE TRIGGER interval_breach AFTER INSERT ON actuals FOR EACH ROW
INSERT INTO alerts
SELECT NEW.id, NEW.ds, NEW.y, f.yhat_lower, f.yhat_upper
FROM forecasts f
WHERE f.id = NEW.id AND f.ds = NEW.ds AND (NEW.y < f.yhat_lower OR NEW.y > f.yhat_upper);

INSERT INTO actuals VALUES ('a', TIMESTAMP '2024-02-01', 11), ('a', TIMESTAMP '2024-02-02', 15);
SELECT * FROM alerts;   -- one row: 2024-02-02, 15 is above the upper bound 12
```

The `forecasts` table can come from any forecasting call (`ts_forecast_by`'s `yhat`,
`yhat_lower`, `yhat_upper`, or `ts_conformal_by`'s `point` / `lower` / `upper` for
distribution-free intervals).

## Recipe 2: refresh the forecasts of the series that changed

A statement trigger runs once per `INSERT` statement and refits only the series that are
stale: no forecasts yet, or a new actual has reached the start of the stored forecast
window. `MERGE INTO` updates the overlapping days, adds the new ones and drops forecasts
for days that are now observed.

```sql
-- DuckDB 2.0+
CREATE TABLE shop_sales AS
SELECT s AS id, TIMESTAMP '2024-01-01' + INTERVAL (i) DAY AS ds, (10 + i % 7)::DOUBLE AS y
FROM range(30) t(i), (VALUES ('a'), ('b')) v(s);

CREATE TABLE shop_fc (id VARCHAR, ds TIMESTAMP, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE);

CREATE TRIGGER refresh_forecasts AFTER INSERT ON shop_sales FOR EACH STATEMENT
MERGE INTO shop_fc t
USING (
    SELECT id, unnest((f).forecast_timestamp) AS ds, unnest((f).point_forecast) AS yhat,
           unnest((f).lower_90) AS yhat_lower, unnest((f).upper_90) AS yhat_upper
    FROM (SELECT id, ts_forecast_agg(ds, y, 'SES', 3, MAP{}) AS f
          FROM shop_sales
          WHERE id IN (SELECT a.id
                       FROM (SELECT id, max(ds) AS last_actual FROM shop_sales GROUP BY id) a
                       LEFT JOIN (SELECT id, min(ds) AS first_fc FROM shop_fc GROUP BY id) f USING (id)
                       WHERE f.first_fc IS NULL OR a.last_actual >= f.first_fc)
          GROUP BY id)
) s
ON t.id = s.id AND t.ds = s.ds
WHEN MATCHED THEN UPDATE SET yhat = s.yhat, yhat_lower = s.yhat_lower, yhat_upper = s.yhat_upper
WHEN NOT MATCHED THEN INSERT VALUES (s.id, s.ds, s.yhat, s.yhat_lower, s.yhat_upper)
WHEN NOT MATCHED BY SOURCE AND t.ds <= (SELECT max(ds) FROM shop_sales x WHERE x.id = t.id) THEN DELETE;

INSERT INTO shop_sales VALUES ('a', TIMESTAMP '2024-01-31', 12);   -- first insert: fits a and b
INSERT INTO shop_sales VALUES ('a', TIMESTAMP '2024-02-01', 14);   -- refits only a
SELECT id, min(ds), max(ds) FROM shop_fc GROUP BY id ORDER BY id;
```

Notes:

- Use `ts_forecast_agg` in the trigger: it forecasts per `GROUP BY` group from columns,
  while the `ts_*_by` macros read a table by name.
- A statement trigger, not a row trigger: DuckDB requires a row trigger to reference `NEW`
  or `OLD` directly in its body, and refitting once per statement batches the work when
  many rows arrive together.
- Each refresh refits the stale series on their full history, so the trigger cost grows
  with history length and model cost. Keep fast models (`SES`, `Naive`, `Theta`) in the
  trigger, or bound the history (`WHERE ds > ...`), and refit slow models on a schedule.

## Without triggers (DuckDB 1.5.6)

Run the trigger bodies as plain statements after loading new actuals, e.g. from cron or an
orchestrator. Recipe 1's body becomes a join over the new batch:

```sql
CREATE TABLE IF NOT EXISTS forecasts (id VARCHAR, ds TIMESTAMP, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE);
CREATE TABLE IF NOT EXISTS new_actuals (id VARCHAR, ds TIMESTAMP, y DOUBLE);
CREATE TABLE IF NOT EXISTS alerts (id VARCHAR, ds TIMESTAMP, y DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE);

INSERT INTO alerts
SELECT n.id, n.ds, n.y, f.yhat_lower, f.yhat_upper
FROM new_actuals n JOIN forecasts f USING (id, ds)
WHERE n.y < f.yhat_lower OR n.y > f.yhat_upper;
```

Recipe 2's `MERGE INTO` statement runs unchanged on 1.5.6.
