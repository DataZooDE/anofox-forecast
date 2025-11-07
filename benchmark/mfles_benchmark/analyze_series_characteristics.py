"""
Analyze statistical characteristics of worst vs. best performing series
to identify patterns that might reveal the bug
"""

import duckdb
import pandas as pd
import numpy as np
from pathlib import Path
from scipy import stats
from statsmodels.tsa.seasonal import STL

def load_m4_data():
    """Load M4 Daily training data."""
    data_path = Path(__file__).parent / "data" / "m4" / "datasets" / "Daily-train.csv"

    # M4 format: first column is series ID, rest are values (with NAs)
    df = pd.read_csv(data_path, index_col=0)

    return df

def get_series_data(df, series_id):
    """Extract a single series as array, removing NAs."""
    series = df.loc[series_id].dropna().values
    return series

def compute_trend_strength(y, period=7):
    """Compute trend strength using STL decomposition."""
    try:
        if len(y) < 2 * period:
            return None

        stl = STL(y, period=period, seasonal=13)
        result = stl.fit()

        # Trend strength: 1 - Var(residual) / Var(detrended)
        detrended = y - result.trend
        var_residual = np.var(result.resid)
        var_detrended = np.var(detrended)

        if var_detrended == 0:
            return 0

        trend_strength = max(0, 1 - var_residual / var_detrended)
        return trend_strength
    except:
        return None

def compute_seasonal_strength(y, period=7):
    """Compute seasonal strength using STL decomposition."""
    try:
        if len(y) < 2 * period:
            return None

        stl = STL(y, period=period, seasonal=13)
        result = stl.fit()

        # Seasonal strength: 1 - Var(residual) / Var(deseasoned)
        deseasoned = y - result.seasonal
        var_residual = np.var(result.resid)
        var_deseasoned = np.var(deseasoned)

        if var_deseasoned == 0:
            return 0

        seasonal_strength = max(0, 1 - var_residual / var_deseasoned)
        return seasonal_strength
    except:
        return None

def compute_statistics(y):
    """Compute comprehensive statistical characteristics."""
    stats_dict = {}

    # Basic statistics
    stats_dict['length'] = len(y)
    stats_dict['mean'] = np.mean(y)
    stats_dict['std'] = np.std(y)
    stats_dict['cv'] = np.std(y) / np.mean(y) if np.mean(y) != 0 else np.inf
    stats_dict['min'] = np.min(y)
    stats_dict['max'] = np.max(y)
    stats_dict['range'] = np.max(y) - np.min(y)
    stats_dict['median'] = np.median(y)

    # Skewness and kurtosis
    stats_dict['skewness'] = stats.skew(y)
    stats_dict['kurtosis'] = stats.kurtosis(y)

    # Trend
    x = np.arange(len(y))
    slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
    stats_dict['trend_slope'] = slope
    stats_dict['trend_r2'] = r_value ** 2
    stats_dict['trend_pvalue'] = p_value

    # Normalized trend slope (% change per observation)
    stats_dict['trend_slope_pct'] = (slope / np.mean(y)) * 100 if np.mean(y) != 0 else 0

    # Volatility (coefficient of variation of differences)
    diffs = np.diff(y)
    stats_dict['volatility'] = np.std(diffs) / np.mean(y) if np.mean(y) != 0 else np.inf
    stats_dict['mean_abs_change'] = np.mean(np.abs(diffs))

    # Outliers (using IQR method)
    q1, q3 = np.percentile(y, [25, 75])
    iqr = q3 - q1
    lower_bound = q1 - 1.5 * iqr
    upper_bound = q3 + 1.5 * iqr
    outliers = np.sum((y < lower_bound) | (y > upper_bound))
    stats_dict['outlier_count'] = outliers
    stats_dict['outlier_pct'] = (outliers / len(y)) * 100

    # Stationarity (Augmented Dickey-Fuller test)
    from statsmodels.tsa.stattools import adfuller
    try:
        adf_result = adfuller(y, autolag='AIC')
        stats_dict['adf_statistic'] = adf_result[0]
        stats_dict['adf_pvalue'] = adf_result[1]
        stats_dict['is_stationary'] = adf_result[1] < 0.05
    except:
        stats_dict['adf_statistic'] = None
        stats_dict['adf_pvalue'] = None
        stats_dict['is_stationary'] = None

    # Seasonality and trend strength
    stats_dict['trend_strength'] = compute_trend_strength(y)
    stats_dict['seasonal_strength'] = compute_seasonal_strength(y)

    # Autocorrelation at lag 7 (weekly seasonality)
    from statsmodels.tsa.stattools import acf
    try:
        acf_values = acf(y, nlags=7, fft=True)
        stats_dict['acf_lag7'] = acf_values[7] if len(acf_values) > 7 else None
    except:
        stats_dict['acf_lag7'] = None

    return stats_dict

def analyze_series_group(df, series_ids, group_name):
    """Analyze a group of series and return statistics."""
    print(f"\n{'='*80}")
    print(f"{group_name}")
    print(f"{'='*80}\n")

    all_stats = []

    for series_id in series_ids:
        try:
            y = get_series_data(df, series_id)
            series_stats = compute_statistics(y)
            series_stats['series_id'] = series_id
            all_stats.append(series_stats)

            print(f"\nSeries: {series_id}")
            print("-" * 40)
            print(f"Length:              {series_stats['length']:>10}")
            print(f"Mean:                {series_stats['mean']:>10.2f}")
            print(f"Std Dev:             {series_stats['std']:>10.2f}")
            print(f"CV:                  {series_stats['cv']:>10.4f}")
            print(f"Range:               {series_stats['range']:>10.2f}")
            print(f"Trend Slope:         {series_stats['trend_slope']:>10.4f}")
            print(f"Trend % per obs:     {series_stats['trend_slope_pct']:>10.4f}%")
            print(f"Trend R²:            {series_stats['trend_r2']:>10.4f}")
            print(f"Volatility:          {series_stats['volatility']:>10.4f}")
            print(f"Outlier %:           {series_stats['outlier_pct']:>10.2f}%")
            print(f"Skewness:            {series_stats['skewness']:>10.4f}")
            print(f"Kurtosis:            {series_stats['kurtosis']:>10.4f}")
            print(f"ACF Lag 7:           {series_stats['acf_lag7']:>10.4f}" if series_stats['acf_lag7'] is not None else "ACF Lag 7:           None")
            print(f"Trend Strength:      {series_stats['trend_strength']:>10.4f}" if series_stats['trend_strength'] is not None else "Trend Strength:      None")
            print(f"Seasonal Strength:   {series_stats['seasonal_strength']:>10.4f}" if series_stats['seasonal_strength'] is not None else "Seasonal Strength:   None")
            print(f"Stationary:          {series_stats['is_stationary']}" if series_stats['is_stationary'] is not None else "Stationary:          None")

        except Exception as e:
            print(f"\nError analyzing {series_id}: {e}")

    return pd.DataFrame(all_stats)

def compare_groups(worst_stats, best_stats):
    """Compare statistical characteristics between worst and best groups."""
    print(f"\n{'='*80}")
    print("COMPARISON: Worst vs. Best Series")
    print(f"{'='*80}\n")

    metrics = ['length', 'mean', 'std', 'cv', 'range', 'trend_slope', 'trend_slope_pct',
               'trend_r2', 'volatility', 'outlier_pct', 'skewness', 'kurtosis',
               'trend_strength', 'seasonal_strength', 'acf_lag7']

    print(f"{'Metric':<25} {'Worst Mean':<15} {'Best Mean':<15} {'Difference':<15} {'Ratio':<10}")
    print("-" * 85)

    for metric in metrics:
        if metric in worst_stats.columns and metric in best_stats.columns:
            worst_mean = worst_stats[metric].mean()
            best_mean = best_stats[metric].mean()
            diff = worst_mean - best_mean
            ratio = worst_mean / best_mean if best_mean != 0 else np.inf

            print(f"{metric:<25} {worst_mean:<15.4f} {best_mean:<15.4f} {diff:<15.4f} {ratio:<10.2f}")

    # Key differences
    print(f"\n{'='*80}")
    print("KEY DIFFERENCES (Ratio > 1.5x or < 0.67x)")
    print(f"{'='*80}\n")

    for metric in metrics:
        if metric in worst_stats.columns and metric in best_stats.columns:
            worst_mean = worst_stats[metric].mean()
            best_mean = best_stats[metric].mean()

            if best_mean != 0:
                ratio = worst_mean / best_mean
                if ratio > 1.5 or ratio < 0.67:
                    print(f"{metric:<25} Ratio: {ratio:.2f}x  (Worst: {worst_mean:.4f}, Best: {best_mean:.4f})")

def main():
    """Run the analysis."""
    print("Loading M4 Daily data...")
    df = load_m4_data()

    # Worst performing series (from forecast comparison)
    worst_series = ['D2191', 'D2168', 'D2139']

    # Best performing series (from forecast comparison)
    best_series = ['D1691', 'D1895', 'D651']

    # Analyze groups
    worst_stats = analyze_series_group(df, worst_series, "WORST PERFORMING SERIES")
    best_stats = analyze_series_group(df, best_series, "BEST PERFORMING SERIES")

    # Compare
    compare_groups(worst_stats, best_stats)

    # Save detailed stats
    output_path = Path(__file__).parent / "results"
    worst_stats.to_csv(output_path / "worst_series_stats.csv", index=False)
    best_stats.to_csv(output_path / "best_series_stats.csv", index=False)

    print(f"\n{'='*80}")
    print("Statistics saved to results/worst_series_stats.csv and results/best_series_stats.csv")
    print(f"{'='*80}")

if __name__ == "__main__":
    main()
