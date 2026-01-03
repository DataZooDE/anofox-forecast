"""
Time series simulation for seasonality detection benchmarking.

Generates synthetic time series with known seasonality characteristics
based on the scenarios from the FDA seasonal analysis simulation study.
"""

import numpy as np
from dataclasses import dataclass
from typing import List, Tuple, Optional
from enum import Enum


class SeasonalityType(Enum):
    """Type of seasonality in the time series."""
    NONE = "none"
    WEAK = "weak"
    MODERATE = "moderate"
    STRONG = "strong"
    VARIABLE = "variable"
    EMERGING = "emerging"
    FADING = "fading"


@dataclass
class SimulationParams:
    """Parameters for time series simulation."""
    n_points: int = 120  # Number of time points
    period: float = 12.0  # True seasonal period
    trend_slope: float = 0.0  # Linear trend slope
    noise_sd: float = 1.0  # Standard deviation of noise
    seed: Optional[int] = None  # Random seed for reproducibility


@dataclass
class SimulatedSeries:
    """A simulated time series with metadata."""
    values: np.ndarray
    true_period: float
    true_strength: float
    seasonality_type: SeasonalityType
    scenario: str
    series_id: int


def generate_seasonal_component(
    n: int,
    period: float,
    amplitude: float,
    phase: float = 0.0
) -> np.ndarray:
    """Generate a sinusoidal seasonal component."""
    t = np.arange(n)
    return amplitude * np.sin(2 * np.pi * t / period + phase)


def generate_trend(n: int, slope: float, intercept: float = 0.0) -> np.ndarray:
    """Generate a linear trend component."""
    t = np.arange(n)
    return intercept + slope * t


def generate_noise(n: int, sd: float, rng: np.random.Generator) -> np.ndarray:
    """Generate Gaussian white noise."""
    return rng.normal(0, sd, n)


def generate_scenario_1_strong_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 1: Strong, stable seasonality.

    Clear sinusoidal pattern with high signal-to-noise ratio.
    Expected: All methods should detect seasonality with high confidence.
    """
    series_list = []
    for i in range(n_series):
        amplitude = rng.uniform(3.0, 5.0)
        phase = rng.uniform(0, 2 * np.pi)

        seasonal = generate_seasonal_component(params.n_points, params.period, amplitude, phase)
        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + seasonal + noise

        # True strength is amplitude relative to total variance
        true_strength = amplitude**2 / (amplitude**2 + params.noise_sd**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.STRONG,
            scenario="strong_seasonal",
            series_id=i
        ))

    return series_list


def generate_scenario_2_weak_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 2: Weak seasonality.

    Low amplitude seasonal pattern with high noise.
    Expected: Methods should show lower confidence, some may miss it.
    """
    series_list = []
    for i in range(n_series):
        amplitude = rng.uniform(0.3, 0.7)
        phase = rng.uniform(0, 2 * np.pi)

        seasonal = generate_seasonal_component(params.n_points, params.period, amplitude, phase)
        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd * 2.0, rng)  # Higher noise

        values = trend + seasonal + noise
        true_strength = amplitude**2 / (amplitude**2 + (params.noise_sd * 2.0)**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.WEAK,
            scenario="weak_seasonal",
            series_id=i
        ))

    return series_list


def generate_scenario_3_no_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 3: No seasonality (null case).

    Pure noise with optional trend, no seasonal component.
    Expected: Methods should correctly identify absence of seasonality.
    """
    series_list = []
    for i in range(n_series):
        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + noise

        series_list.append(SimulatedSeries(
            values=values,
            true_period=0.0,  # No true period
            true_strength=0.0,
            seasonality_type=SeasonalityType.NONE,
            scenario="no_seasonal",
            series_id=i
        ))

    return series_list


def generate_scenario_4_trending_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 4: Seasonality with strong trend.

    Clear seasonal pattern but with strong linear trend that may
    obscure the seasonality for some methods.
    Expected: Robust methods should still detect seasonality.
    """
    series_list = []
    for i in range(n_series):
        amplitude = rng.uniform(2.0, 4.0)
        phase = rng.uniform(0, 2 * np.pi)
        strong_slope = rng.uniform(0.3, 0.5)

        seasonal = generate_seasonal_component(params.n_points, params.period, amplitude, phase)
        trend = generate_trend(params.n_points, strong_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + seasonal + noise
        true_strength = amplitude**2 / (amplitude**2 + params.noise_sd**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.MODERATE,
            scenario="trending_seasonal",
            series_id=i
        ))

    return series_list


def generate_scenario_5_variable_amplitude(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 5: Time-varying amplitude (amplitude modulation).

    Seasonal pattern with amplitude that changes over time.
    Expected: Wavelet methods may detect this better than FFT.
    """
    series_list = []
    for i in range(n_series):
        base_amplitude = rng.uniform(2.0, 4.0)
        phase = rng.uniform(0, 2 * np.pi)

        t = np.arange(params.n_points)
        # Amplitude varies sinusoidally over time
        amplitude_mod = base_amplitude * (1 + 0.5 * np.sin(2 * np.pi * t / (params.n_points / 2)))
        seasonal = amplitude_mod * np.sin(2 * np.pi * t / params.period + phase)

        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + seasonal + noise
        true_strength = base_amplitude**2 / (base_amplitude**2 + params.noise_sd**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.VARIABLE,
            scenario="variable_amplitude",
            series_id=i
        ))

    return series_list


def generate_scenario_6_emerging_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 6: Emerging seasonality.

    No seasonality in first half, strong seasonality in second half.
    Expected: Change detection methods should identify transition.
    """
    series_list = []
    for i in range(n_series):
        amplitude = rng.uniform(3.0, 5.0)
        phase = rng.uniform(0, 2 * np.pi)

        t = np.arange(params.n_points)
        midpoint = params.n_points // 2

        # Sigmoid transition from 0 to full amplitude
        transition_width = params.n_points // 10
        transition = 1 / (1 + np.exp(-(t - midpoint) / transition_width))

        seasonal = amplitude * transition * np.sin(2 * np.pi * t / params.period + phase)
        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + seasonal + noise
        # True strength is for the second half only
        true_strength = amplitude**2 / (amplitude**2 + params.noise_sd**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.EMERGING,
            scenario="emerging_seasonal",
            series_id=i
        ))

    return series_list


def generate_scenario_7_fading_seasonal(
    n_series: int,
    params: SimulationParams,
    rng: np.random.Generator
) -> List[SimulatedSeries]:
    """
    Scenario 7: Fading seasonality.

    Strong seasonality in first half, fading to none in second half.
    Expected: Change detection methods should identify transition.
    """
    series_list = []
    for i in range(n_series):
        amplitude = rng.uniform(3.0, 5.0)
        phase = rng.uniform(0, 2 * np.pi)

        t = np.arange(params.n_points)
        midpoint = params.n_points // 2

        # Inverse sigmoid transition from full to 0
        transition_width = params.n_points // 10
        transition = 1 - 1 / (1 + np.exp(-(t - midpoint) / transition_width))

        seasonal = amplitude * transition * np.sin(2 * np.pi * t / params.period + phase)
        trend = generate_trend(params.n_points, params.trend_slope, 10.0)
        noise = generate_noise(params.n_points, params.noise_sd, rng)

        values = trend + seasonal + noise
        # True strength is for the first half only
        true_strength = amplitude**2 / (amplitude**2 + params.noise_sd**2)

        series_list.append(SimulatedSeries(
            values=values,
            true_period=params.period,
            true_strength=true_strength,
            seasonality_type=SeasonalityType.FADING,
            scenario="fading_seasonal",
            series_id=i
        ))

    return series_list


def generate_all_scenarios(
    n_series_per_scenario: int = 500,
    params: Optional[SimulationParams] = None,
    seed: int = 42
) -> List[SimulatedSeries]:
    """
    Generate time series for all scenarios.

    Args:
        n_series_per_scenario: Number of series to generate per scenario
        params: Simulation parameters (uses defaults if None)
        seed: Random seed for reproducibility

    Returns:
        List of all simulated series across all scenarios
    """
    if params is None:
        params = SimulationParams()

    rng = np.random.default_rng(seed)

    all_series = []

    # Generate each scenario
    scenarios = [
        ("strong_seasonal", generate_scenario_1_strong_seasonal),
        ("weak_seasonal", generate_scenario_2_weak_seasonal),
        ("no_seasonal", generate_scenario_3_no_seasonal),
        ("trending_seasonal", generate_scenario_4_trending_seasonal),
        ("variable_amplitude", generate_scenario_5_variable_amplitude),
        ("emerging_seasonal", generate_scenario_6_emerging_seasonal),
        ("fading_seasonal", generate_scenario_7_fading_seasonal),
    ]

    for name, generator in scenarios:
        series = generator(n_series_per_scenario, params, rng)
        all_series.extend(series)
        print(f"Generated {len(series)} series for scenario: {name}")

    print(f"\nTotal series generated: {len(all_series)}")
    return all_series


def series_to_dict(series: SimulatedSeries) -> dict:
    """Convert a SimulatedSeries to a dictionary for DataFrame creation."""
    return {
        'series_id': series.series_id,
        'scenario': series.scenario,
        'seasonality_type': series.seasonality_type.value,
        'true_period': series.true_period,
        'true_strength': series.true_strength,
        'values': series.values.tolist()
    }


if __name__ == "__main__":
    # Test generation
    params = SimulationParams(n_points=120, period=12.0, noise_sd=1.0)
    series = generate_all_scenarios(n_series_per_scenario=10, params=params, seed=42)

    # Print summary
    from collections import Counter
    scenario_counts = Counter(s.scenario for s in series)
    print("\nScenario counts:")
    for scenario, count in sorted(scenario_counts.items()):
        print(f"  {scenario}: {count}")
