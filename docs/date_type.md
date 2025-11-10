# Date Column Type Support

This document describes which date column types are supported by each function in the anofox-forecast extension.

## Overview

The anofox-forecast extension supports three date column types:
- **INTEGER / BIGINT**: Sequential indices (1, 2, 3, ...)
- **DATE**: SQL DATE type (2024-01-01, 2024-01-02, ...)
- **TIMESTAMP**: SQL TIMESTAMP type with full date and time information

### TIMESTAMP Precision Types (SQL Only)

When creating timestamps directly in SQL, all precision types are supported:
- **TIMESTAMP** (microsecond precision) - Default, recommended
- **TIMESTAMP_S** (second precision)
- **TIMESTAMP_MS** (millisecond precision)
- **TIMESTAMP_NS** (nanosecond precision)

**Important**: While SQL-created `TIMESTAMP_NS` works, pandas datetime64[ns] creates a TIMESTAMP_NS type that is NOT supported. 

## Forecasting Functions

All core forecasting functions support all three date types.

| Function | INTEGER | BIGINT | DATE | TIMESTAMP | Notes |
|----------|---------|--------|------|-----------|-------|
| `ts_forecast` | ✅ | ✅ | ✅ | ✅ | Single series forecasting |
| `TS_FORECAST_AGG` | ✅ | ✅ | ✅ | ✅ | Aggregate forecast function |
| `ts_forecast_by` | ✅ | ✅ | ✅ | ✅ | Multiple series forecasting |

### Example Usage

```sql
-- INTEGER date column
CREATE TABLE sales_int AS
SELECT i AS date_col, 100.0 + i * 5 AS sales
FROM generate_series(1, 100) t(i);

SELECT * FROM ts_forecast(sales_int, date_col, sales, 'AutoARIMA', 10,
    {'seasonal_period': 7});

-- DATE column
CREATE TABLE sales_date AS
SELECT ('2024-01-01'::DATE + INTERVAL (i) DAY)::DATE AS date_col,
       100.0 + i * 5 AS sales
FROM generate_series(1, 100) t(i);

SELECT * FROM ts_forecast(sales_date, date_col, sales, 'AutoARIMA', 10,
    {'seasonal_period': 7});

-- TIMESTAMP column
CREATE TABLE sales_ts AS
SELECT ('2024-01-01'::TIMESTAMP + INTERVAL (i) DAY) AS date_col,
       100.0 + i * 5 AS sales
FROM generate_series(1, 100) t(i);

SELECT * FROM ts_forecast(sales_ts, date_col, sales, 'AutoARIMA', 10,
    {'seasonal_period': 7});
```

## Data Preparation Functions

Data preparation macros have varying levels of support for different date types.

### Full Support (All Date Types)

These functions work with INTEGER, DATE, and TIMESTAMP:

| Function | Description |
|----------|-------------|
| `TS_DROP_CONSTANT` | Drop constant series |
| `TS_DROP_ZEROS` | Drop series with all zeros |
| `TS_DROP_SHORT` | Drop short series |
| `TS_DROP_LEADING_ZEROS` | Drop leading zeros |
| `TS_DROP_TRAILING_ZEROS` | Drop trailing zeros |
| `TS_DROP_EDGE_ZEROS` | Drop leading and trailing zeros |
| `TS_FILL_NULLS_CONST` | Fill nulls with constant |
| `TS_FILL_NULLS_FORWARD` | Forward fill nulls |
| `TS_FILL_NULLS_BACKWARD` | Backward fill nulls |
| `TS_FILL_NULLS_MEAN` | Fill nulls with series mean |

### DATE/TIMESTAMP Only

These functions require DATE or TIMESTAMP types because they perform date arithmetic:

| Function | INTEGER Version | Description |
|----------|----------------|-------------|
| `TS_FILL_GAPS` | `TS_FILL_GAPS_INT` | Fill missing time gaps |
| `TS_FILL_FORWARD` | `TS_FILL_FORWARD_INT` | Extend series forward |
| `TS_DROP_GAPPY` | `TS_DROP_GAPPY_INT` | Drop series with excessive gaps |
| `TS_FILL_NULLS_INTERPOLATE` | `TS_FILL_NULLS_INTERPOLATE_INT` | Linear interpolation |

**Note**: INTEGER versions of these functions are available in `sql/data_preparation_integer.sql`. Load this file to use them:

```sql
-- Load INTEGER-specific data prep functions
LOAD 'sql/data_preparation_integer.sql';

-- Use INTEGER version
SELECT * FROM TS_FILL_GAPS_INT(my_table, series_id, date_col, value);
```

## Python/Pandas Integration

When using pandas DataFrames with DuckDB, be aware of date type conversions:

### Integer Date Columns

```python
import duckdb
import pandas as pd

# INTEGER date column - works directly
df = pd.DataFrame({
    'date_col': range(1, 101),
    'value': [100.0 + i * 5 for i in range(100)]
})

con = duckdb.connect()
con.execute("CREATE TABLE data AS SELECT * FROM df")
result = con.execute("SELECT * FROM ts_forecast(data, date_col, value, 'Naive', 10, NULL)").fetchdf()
```

### Datetime Date Columns

```python
import pandas as pd

# Pandas datetime64 → DATE (recommended)
df = pd.DataFrame({
    'date_col': pd.date_range('2024-01-01', periods=100, freq='D'),
    'value': [100.0 + i * 5 for i in range(100)]
})

# Convert to date type to avoid TIMESTAMP_NS issues
df['date_col'] = df['date_col'].dt.date

con.execute("CREATE TABLE data AS SELECT * FROM df")
result = con.execute("SELECT * FROM ts_forecast(data, date_col, value, 'Naive', 10, NULL)").fetchdf()
```

### Python datetime.date Objects

```python
from datetime import datetime, timedelta

# Python date objects work directly
base_date = datetime(2024, 1, 1).date()
df = pd.DataFrame({
    'date_col': [base_date + timedelta(days=i) for i in range(100)],
    'value': [100.0 + i * 5 for i in range(100)]
})

con.execute("CREATE TABLE data AS SELECT * FROM df")
result = con.execute("SELECT * FROM ts_forecast(data, date_col, value, 'Naive', 10, NULL)").fetchdf()
```

## Common Issues

### TIMESTAMP_NS Error

**Problem**: `Binder Error: First argument (date column) must be INTEGER, BIGINT, DATE, or TIMESTAMP, got TIMESTAMP_NS`

**Cause**: Pandas creates datetime64[ns] (nanosecond precision) by default, which DuckDB treats as TIMESTAMP_NS when loaded through the Python API. While SQL-created `TIMESTAMP_NS` (using `::TIMESTAMP_NS` cast) works fine, pandas-created TIMESTAMP_NS does not.

**Why the distinction?**
- **SQL**: `SELECT '2024-01-01'::TIMESTAMP_NS` → Works ✅
- **Python/Pandas**: `pd.date_range()` → Creates datetime64[ns] → DuckDB sees as TIMESTAMP_NS → Fails ❌

**Solution**: Convert pandas datetime64 to DATE type:
```python
df['date_col'] = df['date_col'].dt.date
```

Or cast explicitly in SQL:
```sql
SELECT * FROM ts_forecast(
    (SELECT *, CAST(date_col AS DATE) AS date_col FROM data),
    date_col, value, 'Naive', 10, NULL
)
```

Or convert to standard TIMESTAMP (microsecond precision):
```sql
SELECT * FROM ts_forecast(
    (SELECT *, CAST(date_col AS TIMESTAMP) AS date_col FROM data),
    date_col, value, 'Naive', 10, NULL
)
```

### Data Prep Functions with INTEGER

**Problem**: `TS_FILL_GAPS` doesn't work with INTEGER date columns.

**Solution**: Use the INTEGER-specific version:
```sql
LOAD 'sql/data_preparation_integer.sql';
SELECT * FROM TS_FILL_GAPS_INT(my_table, series_id, date_col, value);
```