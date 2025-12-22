#!/usr/bin/env python3
"""
Generate expected feature values from tsfresh for C++ unit tests.

This script calculates all features from features_overrides.json using tsfresh
and outputs the expected values in a format that can be embedded in C++ tests.
The script uses the same test series (365 values, seed=42) as defined in create_data.sql.
"""

import json
import math
import random
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from tsfresh import extract_features

# Generate the same test series (365 values, seed=42)
random.seed(42)

# Fixed parameters for series_id=1 (simulating the SQL logic)
base_level = 100 + int(random.random() * 900)
trend_slope = (random.random() * 2 - 1) * 0.5
weekly_amplitude = int(random.random() * 50)
monthly_amplitude = int(random.random() * 30)
noise_level = 5 + int(random.random() * 15)

# Generate 365 values
values = []
for d in range(365):
    value = max(0, 
        base_level 
        + trend_slope * d
        + weekly_amplitude * math.sin(d * 2 * math.pi / 7)
        + monthly_amplitude * math.sin(d * 2 * math.pi / 30)
        + (random.random() * noise_level - noise_level / 2)
    )
    values.append(round(value, 2))

# Load feature overrides
script_dir = Path(__file__).parent
repo_root = script_dir.parent.parent
features_json = repo_root / "benchmark" / "timeseries_features" / "data" / "features_overrides.json"

with open(features_json, 'r') as f:
    features = json.load(f)

# Prepare dataframe for tsfresh
df = pd.DataFrame({
    'id': [1] * 365,
    'time': pd.date_range('2023-01-01', periods=365, freq='D'),
    'value': values
})

# Load feature parameters in tsfresh format
fc_parameters = {}
for entry in features:
    feature = entry['feature']
    params = entry.get('params', {})
    if params:
        # Convert params to tsfresh format
        tsfresh_params = {}
        for k, v in params.items():
            # Convert types appropriately
            if isinstance(v, bool):
                tsfresh_params[k] = v
            elif isinstance(v, (int, float)):
                tsfresh_params[k] = v
            elif isinstance(v, str):
                tsfresh_params[k] = v
        fc_parameters[feature] = [tsfresh_params]
    else:
        fc_parameters[feature] = None

# Calculate features
try:
    result = extract_features(
        df,
        column_id='id',
        column_sort='time',
        column_value='value',
        default_fc_parameters=fc_parameters,
        n_jobs=1,
        disable_progressbar=True
    )
    # Debug: print available columns
    print(f"// DEBUG: tsfresh generated {len(result.columns)} columns", file=sys.stderr)
    print(f"// DEBUG: First 10 columns: {list(result.columns[:10])}", file=sys.stderr)
except Exception as e:
    print(f"Error calculating features: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    sys.exit(1)

# Extract values for each feature
expected_values = {}
for entry in features:
    feature = entry['feature']
    params = entry.get('params', {})
    
    # Build column name (tsfresh format)
    if params:
        param_parts = []
        for k, v in sorted(params.items()):
            if isinstance(v, bool):
                param_parts.append(f"{k}_{str(v).lower()}")
            elif isinstance(v, (int, float)):
                param_parts.append(f"{k}_{v}")
            else:
                param_parts.append(f"{k}_{v}")
        param_suffix = '__' + '__'.join(param_parts)
    else:
        param_suffix = ''
    
    # tsfresh prefixes with "value__"
    column_name = f"value__{feature}{param_suffix}"
    
    # Try exact match first
    if column_name in result.columns:
        value = result[column_name].iloc[0]
    else:
        # Try to find column with different parameter formatting
        matching_cols = [col for col in result.columns if col.startswith(f"value__{feature}__")]
        if matching_cols:
            # Use first match
            column_name = matching_cols[0]
            value = result[column_name].iloc[0]
        elif f"value__{feature}" in result.columns:
            # No parameters
            value = result[f"value__{feature}"].iloc[0]
        else:
            value = None
    
    if value is None or pd.isna(value):
        expected_values[feature] = None
    else:
        expected_values[feature] = float(value)

# Output as C++ code snippets for each test
print("// Expected values from tsfresh Python library")
print("// Generated for TEST_SERIES (365 values, seed=42)")
print()

def format_cpp_double(value):
    """Format a Python float as a C++ double literal."""
    # Handle very small values that might be effectively zero
    if abs(value) < 1e-200:
        return "0.0"
    elif abs(value) > 1e15 or (abs(value) < 1e-6 and value != 0):
        # Use scientific notation for very large or very small numbers
        return f"{value:.15e}"
    else:
        # Use regular decimal notation
        # Format with enough precision but remove trailing zeros
        formatted = f"{value:.15f}".rstrip('0').rstrip('.')
        if not formatted.replace('.', '').replace('-', '').replace('+', ''):
            return "0.0"
        # Ensure it ends with .0 if it's an integer
        if '.' not in formatted:
            return formatted + ".0"
        return formatted

for entry in features:
    feature = entry['feature']
    expected = expected_values.get(feature)
    
    if expected is None:
        print(f"// {feature}: NaN or not found")
    else:
        cpp_value = format_cpp_double(expected)
        print(f"const double {feature.upper().replace('-', '_')}_EXPECTED = {cpp_value};  // tsfresh: {expected}")

