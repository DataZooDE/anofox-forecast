"""
Python/Pandas integration tests for date column type handling.

Tests that pandas DataFrames with various date column types (int, datetime64, etc.)
are correctly converted and work with all forecasting and data prep functions.
"""

import unittest
import duckdb
import pandas as pd
import numpy as np
from pathlib import Path
from datetime import datetime, timedelta


class TestPandasDateTypes(unittest.TestCase):
    """Test pandas DataFrame integration with different date column types."""

    @classmethod
    def setUpClass(cls):
        """Load the extension once for all tests."""
        # Find the extension
        extension_path = Path(__file__).parent.parent.parent / 'build' / 'release' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'

        if not extension_path.exists():
            raise FileNotFoundError(f"Extension not found at {extension_path}. Please build it first.")

        cls.extension_path = str(extension_path)

    def setUp(self):
        """Create a new connection for each test."""
        self.con = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})
        self.con.execute(f"LOAD '{self.extension_path}'")

    def tearDown(self):
        """Close connection after each test."""
        self.con.close()

    # ========================================
    # Integer Date Columns
    # ========================================

    def test_integer_date_column_ts_forecast(self):
        """Test ts_forecast with integer date column from pandas."""
        df = pd.DataFrame({
            'date_col': range(1, 31),
            'value': [100.0 + i * 5 for i in range(1, 31)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 10)

    def test_integer_date_column_ts_forecast_by(self):
        """Test ts_forecast_by with integer date column from pandas."""
        df = pd.DataFrame({
            'date_col': list(range(1, 31)) * 3,
            'series_id': ['A'] * 30 + ['B'] * 30 + ['C'] * 30,
            'value': [100.0 + i * 5 for i in range(90)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(DISTINCT series_id) FROM ts_forecast_by(test_data, series_id, date_col, value, 'Naive', 5, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 3)

    # ========================================
    # Pandas datetime64 (Default) Date Columns
    # ========================================

    def test_datetime64_ts_forecast(self):
        """Test ts_forecast with pandas datetime64 (default pd.to_datetime result)."""
        # This is the default pandas datetime type
        df = pd.DataFrame({
            'date_col': pd.date_range('2024-01-01', periods=30, freq='D'),
            'value': [100.0 + i * 5 for i in range(30)]
        })

        # Convert to date type to avoid TIMESTAMP_NS issue
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 10)

    def test_datetime64_ts_forecast_by(self):
        """Test ts_forecast_by with pandas datetime64."""
        dates = pd.date_range('2024-01-01', periods=30, freq='D')
        df = pd.DataFrame({
            'date_col': list(dates) * 3,
            'series_id': ['A'] * 30 + ['B'] * 30 + ['C'] * 30,
            'value': [100.0 + i * 5 for i in range(90)]
        })

        # Convert to date type
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(DISTINCT series_id) FROM ts_forecast_by(test_data, series_id, date_col, value, 'Naive', 5, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 3)

    # ========================================
    # Python datetime.date Objects
    # ========================================

    def test_python_date_objects_ts_forecast(self):
        """Test ts_forecast with Python datetime.date objects."""
        base_date = datetime(2024, 1, 1).date()
        df = pd.DataFrame({
            'date_col': [base_date + timedelta(days=i) for i in range(30)],
            'value': [100.0 + i * 5 for i in range(30)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 10)

    # ========================================
    # Data Prep Functions with Different Date Types
    # ========================================

    def test_ts_fill_gaps_integer_dates(self):
        """Test ts_fill_gaps with integer date column."""
        df = pd.DataFrame({
            'date_col': [i for i in range(1, 31) if i not in [10, 15, 20]],
            'series_id': ['series_1'] * 27,
            'value': [100.0] * 27
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_fill_gaps(test_data, series_id, date_col, value) WHERE group_col = 'series_1'"
        ).fetchone()[0]

        self.assertEqual(result, 30)

    def test_ts_fill_gaps_datetime_dates(self):
        """Test ts_fill_gaps with datetime date column."""
        dates = [pd.Timestamp('2024-01-01') + pd.Timedelta(days=i) for i in range(30) if i not in [9, 14, 19]]
        df = pd.DataFrame({
            'date_col': dates,
            'series_id': ['series_1'] * 27,
            'value': [100.0] * 27
        })

        # Convert to date type
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_fill_gaps(test_data, series_id, date_col, value) WHERE group_col = 'series_1'"
        ).fetchone()[0]

        self.assertEqual(result, 30)

    def test_ts_fill_nulls_forward_integer_dates(self):
        """Test ts_fill_nulls_forward with integer dates."""
        df = pd.DataFrame({
            'date_col': range(1, 31),
            'series_id': ['series_1'] * 30,
            'value': [None if 5 <= i <= 7 else 100.0 for i in range(1, 31)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT SUM(CASE WHEN value_col IS NULL THEN 1 ELSE 0 END) FROM ts_fill_nulls_forward(test_data, series_id, date_col, value)"
        ).fetchone()[0]

        self.assertLess(result, 3)

    def test_ts_fill_nulls_forward_datetime_dates(self):
        """Test ts_fill_nulls_forward with datetime dates."""
        df = pd.DataFrame({
            'date_col': pd.date_range('2024-01-01', periods=30, freq='D'),
            'series_id': ['series_1'] * 30,
            'value': [None if 5 <= i <= 7 else 100.0 for i in range(30)]
        })

        # Convert to date type
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT SUM(CASE WHEN value_col IS NULL THEN 1 ELSE 0 END) FROM ts_fill_nulls_forward(test_data, series_id, date_col, value)"
        ).fetchone()[0]

        self.assertLess(result, 3)

    def test_ts_drop_constant_integer_dates(self):
        """Test ts_drop_constant with integer dates."""
        df = pd.DataFrame({
            'date_col': list(range(1, 31)) * 2,
            'series_id': ['constant'] * 30 + ['varying'] * 30,
            'value': [100.0] * 30 + [100.0 + i for i in range(30)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(DISTINCT series_id) FROM ts_drop_constant(test_data, series_id, value)"
        ).fetchone()[0]

        self.assertEqual(result, 1)

    def test_ts_drop_constant_datetime_dates(self):
        """Test ts_drop_constant with datetime dates."""
        dates = pd.date_range('2024-01-01', periods=30, freq='D')
        df = pd.DataFrame({
            'date_col': list(dates) * 2,
            'series_id': ['constant'] * 30 + ['varying'] * 30,
            'value': [100.0] * 30 + [100.0 + i for i in range(30)]
        })

        # Convert to date type
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(DISTINCT series_id) FROM ts_drop_constant(test_data, series_id, value)"
        ).fetchone()[0]

        self.assertEqual(result, 1)

    # ========================================
    # AutoARIMA with Different Date Types
    # ========================================

    def test_autoarima_integer_dates(self):
        """Test AutoARIMA with integer date column."""
        df = pd.DataFrame({
            'date_col': range(1, 51),
            'value': [100.0 + i * 5 + np.sin(i/7) * 10 for i in range(50)]
        })

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT TS_FORECAST_AGG(date_col, value, 'AutoARIMA', 10, {'seasonal_period': 7}) AS result FROM test_data"
        ).fetchone()[0]

        self.assertEqual(len(result['point_forecast']), 10)
        self.assertEqual(len(result['lower']), 10)
        self.assertEqual(len(result['upper']), 10)

    def test_autoarima_datetime_dates(self):
        """Test AutoARIMA with datetime date column."""
        df = pd.DataFrame({
            'date_col': pd.date_range('2024-01-01', periods=50, freq='D'),
            'value': [100.0 + i * 5 + np.sin(i/7) * 10 for i in range(50)]
        })

        # Convert to date type
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT TS_FORECAST_AGG(date_col, value, 'AutoARIMA', 10, {'seasonal_period': 7}) AS result FROM test_data"
        ).fetchone()[0]

        self.assertEqual(len(result['point_forecast']), 10)
        self.assertEqual(len(result['lower']), 10)
        self.assertEqual(len(result['upper']), 10)

    # ========================================
    # Edge Cases
    # ========================================

    def test_mixed_integer_types(self):
        """Test that both INT and BIGINT work."""
        # Regular int
        df1 = pd.DataFrame({
            'date_col': pd.Series(range(1, 31), dtype='int32'),
            'value': [100.0 + i * 5 for i in range(30)]
        })

        self.con.execute("CREATE TABLE test_data1 AS SELECT * FROM df1")
        result1 = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data1, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        # Bigint
        df2 = pd.DataFrame({
            'date_col': pd.Series(range(1, 31), dtype='int64'),
            'value': [100.0 + i * 5 for i in range(30)]
        })

        self.con.execute("CREATE TABLE test_data2 AS SELECT * FROM df2")
        result2 = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data2, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        self.assertEqual(result1, 10)
        self.assertEqual(result2, 10)

    def test_pandas_timestamp_conversion(self):
        """Test that pandas Timestamp objects are correctly converted."""
        df = pd.DataFrame({
            'date_col': [pd.Timestamp('2024-01-01') + pd.Timedelta(days=i) for i in range(30)],
            'value': [100.0 + i * 5 for i in range(30)]
        })

        # Convert to date type to avoid TIMESTAMP_NS
        df['date_col'] = df['date_col'].dt.date

        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")
        result = self.con.execute(
            "SELECT COUNT(*) FROM ts_forecast(test_data, date_col, value, 'Naive', 10, NULL)"
        ).fetchone()[0]

        self.assertEqual(result, 10)

    def test_pandas_datetime64_without_conversion_fails(self):
        """Test that pandas datetime64[ns] requires conversion to work.

        Pandas creates datetime64[ns] which DuckDB sees as TIMESTAMP_NS.
        While SQL-created TIMESTAMP_NS works, pandas-created TIMESTAMP_NS does NOT.
        This test documents this behavior and verifies the error message.
        """
        # This tests the raw pandas datetime64[ns] type
        df = pd.DataFrame({
            'date_col': pd.date_range('2024-01-01', periods=30, freq='D'),
            'value': [100.0 + i * 5 for i in range(30)]
        })

        # DO NOT convert - verify it fails with clear error
        self.con.execute("CREATE TABLE test_data AS SELECT * FROM df")

        # Check the type DuckDB sees
        dtype = self.con.execute("SELECT typeof(date_col) FROM test_data LIMIT 1").fetchone()[0]
        self.assertEqual(dtype, 'TIMESTAMP_NS')

        # This should fail with a clear error message
        with self.assertRaises(Exception) as context:
            self.con.execute(
                "SELECT COUNT(*) FROM ts_forecast(test_data, date_col, value, 'Naive', 10, NULL)"
            )

        # Verify the error message is helpful
        self.assertIn('TIMESTAMP_NS', str(context.exception))
        self.assertIn('INTEGER, BIGINT, DATE, or TIMESTAMP', str(context.exception))


if __name__ == '__main__':
    unittest.main()
