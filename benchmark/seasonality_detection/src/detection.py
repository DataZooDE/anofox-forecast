"""
Seasonality detection methods using the DuckDB extension.

Runs the fdars-core based detection functions on simulated time series
and returns results for evaluation.
"""

import duckdb
import numpy as np
from dataclasses import dataclass
from typing import List, Dict, Any, Optional
from pathlib import Path

try:
    from .simulation import SimulatedSeries
except ImportError:
    from simulation import SimulatedSeries


@dataclass
class DetectionResult:
    """Result from a single detection method on a single series."""
    series_id: int
    scenario: str
    method: str
    detected_period: Optional[float]
    confidence: float
    strength: float
    is_seasonal: bool
    classification: Optional[str]
    extra: Dict[str, Any]


def get_extension_path() -> Path:
    """Get the path to the built extension."""
    base = Path(__file__).parent.parent.parent.parent
    build_dir = base / "build" / "release"
    if not build_dir.exists():
        build_dir = base / "build" / "debug"
    return build_dir


def create_connection() -> duckdb.DuckDBPyConnection:
    """Create a DuckDB connection with the extension loaded."""
    # Allow unsigned extensions for local development builds
    conn = duckdb.connect(config={"allow_unsigned_extensions": "true"})

    ext_path = get_extension_path()
    ext_file = ext_path / "extension" / "anofox_forecast" / "anofox_forecast.duckdb_extension"

    if ext_file.exists():
        conn.execute(f"LOAD '{ext_file}'")
    else:
        # Try from build root
        conn.execute(f"LOAD '{ext_path / 'anofox_forecast.duckdb_extension'}'")

    return conn


def values_to_sql_array(values: np.ndarray) -> str:
    """Convert numpy array to DuckDB array literal."""
    formatted = ", ".join(f"{v:.10g}" for v in values)
    return f"[{formatted}]"


def run_fft_detection(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries
) -> DetectionResult:
    """Run FFT-based period detection."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_detect_periods({arr}, 'fft') as result"

    try:
        result = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="fft",
            detected_period=result.get("primary_period"),
            confidence=result.get("confidence", 0.0),
            strength=result.get("confidence", 0.0),
            is_seasonal=result.get("n_periods", 0) > 0,
            classification=None,
            extra={"n_periods": result.get("n_periods", 0)}
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="fft",
            detected_period=None,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_acf_detection(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries
) -> DetectionResult:
    """Run ACF-based period detection."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_estimate_period_acf({arr}) as result"

    try:
        result = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="acf",
            detected_period=result.get("period"),
            confidence=result.get("confidence", 0.0),
            strength=result.get("confidence", 0.0),
            is_seasonal=result.get("period", 0) > 0,
            classification=None,
            extra={"acf_values": result.get("acf_values", [])}
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="acf",
            detected_period=None,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_variance_strength(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries,
    period: float
) -> DetectionResult:
    """Run variance-based seasonal strength computation."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_seasonal_strength({arr}, {period}, 'variance') as strength"

    try:
        strength = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="variance_strength",
            detected_period=period,
            confidence=strength if strength else 0.0,
            strength=strength if strength else 0.0,
            is_seasonal=(strength or 0.0) > 0.3,  # Threshold for "seasonal"
            classification=None,
            extra={}
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="variance_strength",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_spectral_strength(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries,
    period: float
) -> DetectionResult:
    """Run spectral-based seasonal strength computation."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_seasonal_strength({arr}, {period}, 'spectral') as strength"

    try:
        strength = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="spectral_strength",
            detected_period=period,
            confidence=strength if strength else 0.0,
            strength=strength if strength else 0.0,
            is_seasonal=(strength or 0.0) > 0.3,
            classification=None,
            extra={}
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="spectral_strength",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_wavelet_strength(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries,
    period: float
) -> DetectionResult:
    """Run wavelet-based seasonal strength computation."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_seasonal_strength({arr}, {period}, 'wavelet') as strength"

    try:
        strength = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="wavelet_strength",
            detected_period=period,
            confidence=strength if strength else 0.0,
            strength=strength if strength else 0.0,
            is_seasonal=(strength or 0.0) > 0.3,
            classification=None,
            extra={}
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="wavelet_strength",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_classification(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries,
    period: float
) -> DetectionResult:
    """Run seasonality classification."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_classify_seasonality({arr}, {period}) as result"

    try:
        result = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="classification",
            detected_period=period,
            confidence=result.get("mean_strength", 0.0),
            strength=result.get("mean_strength", 0.0),
            is_seasonal=result.get("is_seasonal", False),
            classification=result.get("classification"),
            extra={
                "cycle_strengths": result.get("cycle_strengths", []),
                "n_cycles": result.get("n_cycles", 0)
            }
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="classification",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_change_detection(
    conn: duckdb.DuckDBPyConnection,
    series: SimulatedSeries,
    period: float
) -> DetectionResult:
    """Run seasonality change detection."""
    arr = values_to_sql_array(series.values)
    query = f"SELECT ts_detect_seasonality_changes({arr}, {period}) as result"

    try:
        result = conn.execute(query).fetchone()[0]

        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="change_detection",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=result.get("n_changes", 0) > 0,
            classification=None,
            extra={
                "n_changes": result.get("n_changes", 0),
                "change_points": result.get("change_points", []),
                "strength_curve": result.get("strength_curve", [])
            }
        )
    except Exception as e:
        return DetectionResult(
            series_id=series.series_id,
            scenario=series.scenario,
            method="change_detection",
            detected_period=period,
            confidence=0.0,
            strength=0.0,
            is_seasonal=False,
            classification=None,
            extra={"error": str(e)}
        )


def run_all_methods(
    series_list: List[SimulatedSeries],
    known_period: float = 12.0,
    show_progress: bool = True
) -> List[DetectionResult]:
    """
    Run all detection methods on all series.

    Args:
        series_list: List of simulated time series
        known_period: The true period for methods that require it
        show_progress: Whether to print progress updates

    Returns:
        List of detection results for all method/series combinations
    """
    conn = create_connection()
    results = []

    methods = [
        ("fft", lambda s: run_fft_detection(conn, s)),
        ("acf", lambda s: run_acf_detection(conn, s)),
        ("variance_strength", lambda s: run_variance_strength(conn, s, known_period)),
        ("spectral_strength", lambda s: run_spectral_strength(conn, s, known_period)),
        ("wavelet_strength", lambda s: run_wavelet_strength(conn, s, known_period)),
        ("classification", lambda s: run_classification(conn, s, known_period)),
        ("change_detection", lambda s: run_change_detection(conn, s, known_period)),
    ]

    n_series = len(series_list)
    n_methods = len(methods)
    total = n_series * n_methods

    for i, series in enumerate(series_list):
        for method_name, method_fn in methods:
            if show_progress and (i * n_methods) % 100 == 0:
                pct = (i * n_methods) / total * 100
                print(f"Progress: {pct:.1f}% ({i}/{n_series} series)")

            result = method_fn(series)
            results.append(result)

    if show_progress:
        print(f"Completed: {len(results)} detection runs")

    conn.close()
    return results


def results_to_dataframe(results: List[DetectionResult]):
    """Convert detection results to a pandas DataFrame."""
    import pandas as pd

    records = []
    for r in results:
        records.append({
            "series_id": r.series_id,
            "scenario": r.scenario,
            "method": r.method,
            "detected_period": r.detected_period,
            "confidence": r.confidence,
            "strength": r.strength,
            "is_seasonal": r.is_seasonal,
            "classification": r.classification,
            "has_error": "error" in r.extra
        })

    return pd.DataFrame(records)


if __name__ == "__main__":
    # Test detection on a small sample
    from .simulation import generate_all_scenarios, SimulationParams

    params = SimulationParams(n_points=120, period=12.0, noise_sd=1.0)
    series = generate_all_scenarios(n_series_per_scenario=5, params=params, seed=42)

    print(f"\nRunning detection on {len(series)} series...")
    results = run_all_methods(series, known_period=12.0)

    df = results_to_dataframe(results)
    print("\nResults summary:")
    print(df.groupby(["scenario", "method"])["is_seasonal"].mean())
