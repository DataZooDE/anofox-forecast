"""
Evaluation metrics for seasonality detection benchmark.

Computes performance metrics comparing detection results to ground truth.
"""

import numpy as np
import pandas as pd
from dataclasses import dataclass
from typing import List, Dict, Tuple, Optional

try:
    from .simulation import SimulatedSeries, SeasonalityType
    from .detection import DetectionResult
except ImportError:
    from simulation import SimulatedSeries, SeasonalityType
    from detection import DetectionResult


@dataclass
class MethodMetrics:
    """Evaluation metrics for a single detection method."""
    method: str
    sensitivity: float  # True positive rate
    specificity: float  # True negative rate
    precision: float  # Positive predictive value
    f1_score: float
    accuracy: float
    period_mae: float  # Mean absolute error for period estimation
    period_mape: float  # Mean absolute percentage error
    strength_correlation: float  # Correlation with true strength
    auc_roc: Optional[float]  # Area under ROC curve


@dataclass
class ScenarioMetrics:
    """Metrics for a specific scenario."""
    scenario: str
    n_series: int
    detection_rate: float
    mean_confidence: float
    mean_strength: float
    period_accuracy: float


def compute_binary_metrics(
    y_true: np.ndarray,
    y_pred: np.ndarray
) -> Tuple[float, float, float, float, float]:
    """
    Compute binary classification metrics.

    Returns:
        sensitivity, specificity, precision, f1_score, accuracy
    """
    tp = np.sum((y_true == 1) & (y_pred == 1))
    tn = np.sum((y_true == 0) & (y_pred == 0))
    fp = np.sum((y_true == 0) & (y_pred == 1))
    fn = np.sum((y_true == 1) & (y_pred == 0))

    sensitivity = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    specificity = tn / (tn + fp) if (tn + fp) > 0 else 0.0
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    f1 = 2 * precision * sensitivity / (precision + sensitivity) if (precision + sensitivity) > 0 else 0.0
    accuracy = (tp + tn) / len(y_true) if len(y_true) > 0 else 0.0

    return sensitivity, specificity, precision, f1, accuracy


def compute_auc_roc(y_true: np.ndarray, scores: np.ndarray) -> float:
    """Compute area under ROC curve."""
    # Sort by scores
    order = np.argsort(scores)[::-1]
    y_sorted = y_true[order]

    # Compute TPR and FPR at each threshold
    n_pos = np.sum(y_true)
    n_neg = len(y_true) - n_pos

    if n_pos == 0 or n_neg == 0:
        return 0.5  # Undefined, return random classifier

    tpr = np.cumsum(y_sorted) / n_pos
    fpr = np.cumsum(1 - y_sorted) / n_neg

    # Prepend (0, 0)
    tpr = np.concatenate([[0], tpr])
    fpr = np.concatenate([[0], fpr])

    # Compute AUC using trapezoidal rule
    # Use trapezoid (numpy 2.0+) or trapz (older numpy)
    try:
        auc = np.trapezoid(tpr, fpr)
    except AttributeError:
        auc = np.trapz(tpr, fpr)
    return auc


def get_ground_truth(series_list: List[SimulatedSeries]) -> pd.DataFrame:
    """Extract ground truth labels from simulated series."""
    records = []
    for s in series_list:
        is_seasonal = s.seasonality_type != SeasonalityType.NONE
        records.append({
            "series_id": s.series_id,
            "scenario": s.scenario,
            "true_period": s.true_period,
            "true_strength": s.true_strength,
            "is_seasonal": is_seasonal,
            "seasonality_type": s.seasonality_type.value
        })
    return pd.DataFrame(records)


def evaluate_method(
    results_df: pd.DataFrame,
    ground_truth_df: pd.DataFrame,
    method: str
) -> MethodMetrics:
    """
    Evaluate a single detection method.

    Args:
        results_df: DataFrame with detection results
        ground_truth_df: DataFrame with ground truth labels
        method: Name of the method to evaluate

    Returns:
        MethodMetrics for the method
    """
    # Filter to this method
    method_df = results_df[results_df["method"] == method].copy()

    # Merge with ground truth
    merged = method_df.merge(
        ground_truth_df,
        on=["series_id", "scenario"],
        suffixes=("_pred", "_true")
    )

    y_true = merged["is_seasonal_true"].astype(int).values
    y_pred = merged["is_seasonal_pred"].astype(int).values

    # Binary metrics
    sens, spec, prec, f1, acc = compute_binary_metrics(y_true, y_pred)

    # AUC-ROC using confidence/strength as score
    scores = merged["confidence"].fillna(0).values
    auc = compute_auc_roc(y_true, scores)

    # Period estimation error (only for series with true seasonality and detected period)
    seasonal_mask = (merged["is_seasonal_true"]) & (merged["detected_period"].notna())
    if seasonal_mask.sum() > 0:
        true_periods = merged.loc[seasonal_mask, "true_period"].values
        pred_periods = merged.loc[seasonal_mask, "detected_period"].values
        period_mae = np.mean(np.abs(pred_periods - true_periods))
        period_mape = np.mean(np.abs(pred_periods - true_periods) / true_periods) * 100
    else:
        period_mae = np.nan
        period_mape = np.nan

    # Strength correlation (only for series with true seasonality)
    seasonal_mask = merged["is_seasonal_true"]
    if seasonal_mask.sum() > 1:
        true_strength = merged.loc[seasonal_mask, "true_strength"].values
        pred_strength = merged.loc[seasonal_mask, "strength"].values
        if np.std(true_strength) > 0 and np.std(pred_strength) > 0:
            strength_corr = np.corrcoef(true_strength, pred_strength)[0, 1]
        else:
            strength_corr = np.nan
    else:
        strength_corr = np.nan

    return MethodMetrics(
        method=method,
        sensitivity=sens,
        specificity=spec,
        precision=prec,
        f1_score=f1,
        accuracy=acc,
        period_mae=period_mae,
        period_mape=period_mape,
        strength_correlation=strength_corr,
        auc_roc=auc
    )


def evaluate_scenario(
    results_df: pd.DataFrame,
    ground_truth_df: pd.DataFrame,
    scenario: str,
    method: str
) -> ScenarioMetrics:
    """Evaluate performance on a specific scenario."""
    # Filter to scenario and method
    mask = (results_df["scenario"] == scenario) & (results_df["method"] == method)
    scenario_df = results_df[mask].copy()

    gt_mask = ground_truth_df["scenario"] == scenario
    gt_df = ground_truth_df[gt_mask]

    n_series = len(scenario_df)
    detection_rate = scenario_df["is_seasonal"].mean()
    mean_confidence = scenario_df["confidence"].mean()
    mean_strength = scenario_df["strength"].mean()

    # Period accuracy: fraction within 10% of true period
    merged = scenario_df.merge(gt_df[["series_id", "scenario", "true_period"]], on=["series_id", "scenario"])
    if merged["true_period"].iloc[0] > 0 and merged["detected_period"].notna().any():
        within_10pct = np.abs(merged["detected_period"] - merged["true_period"]) / merged["true_period"] < 0.1
        period_accuracy = within_10pct.mean()
    else:
        period_accuracy = np.nan

    return ScenarioMetrics(
        scenario=scenario,
        n_series=n_series,
        detection_rate=detection_rate,
        mean_confidence=mean_confidence,
        mean_strength=mean_strength,
        period_accuracy=period_accuracy
    )


def evaluate_all(
    results: List[DetectionResult],
    series_list: List[SimulatedSeries]
) -> Dict[str, pd.DataFrame]:
    """
    Evaluate all methods and scenarios.

    Returns:
        Dictionary with:
        - "method_metrics": DataFrame of MethodMetrics
        - "scenario_metrics": DataFrame of ScenarioMetrics
        - "confusion_matrix": Confusion matrix for each method
    """
    try:
        from .detection import results_to_dataframe
    except ImportError:
        from detection import results_to_dataframe

    results_df = results_to_dataframe(results)
    ground_truth_df = get_ground_truth(series_list)

    # Method-level metrics
    methods = results_df["method"].unique()
    method_metrics = []
    for method in methods:
        metrics = evaluate_method(results_df, ground_truth_df, method)
        method_metrics.append({
            "method": metrics.method,
            "sensitivity": metrics.sensitivity,
            "specificity": metrics.specificity,
            "precision": metrics.precision,
            "f1_score": metrics.f1_score,
            "accuracy": metrics.accuracy,
            "period_mae": metrics.period_mae,
            "period_mape": metrics.period_mape,
            "strength_correlation": metrics.strength_correlation,
            "auc_roc": metrics.auc_roc
        })
    method_df = pd.DataFrame(method_metrics)

    # Scenario-level metrics
    scenarios = results_df["scenario"].unique()
    scenario_metrics = []
    for scenario in scenarios:
        for method in methods:
            metrics = evaluate_scenario(results_df, ground_truth_df, scenario, method)
            scenario_metrics.append({
                "scenario": metrics.scenario,
                "method": method,
                "n_series": metrics.n_series,
                "detection_rate": metrics.detection_rate,
                "mean_confidence": metrics.mean_confidence,
                "mean_strength": metrics.mean_strength,
                "period_accuracy": metrics.period_accuracy
            })
    scenario_df = pd.DataFrame(scenario_metrics)

    return {
        "method_metrics": method_df,
        "scenario_metrics": scenario_df,
        "results": results_df,
        "ground_truth": ground_truth_df
    }


def print_summary(evaluation: Dict[str, pd.DataFrame]) -> None:
    """Print a summary of the evaluation results."""
    method_df = evaluation["method_metrics"]
    scenario_df = evaluation["scenario_metrics"]

    print("\n" + "=" * 60)
    print("SEASONALITY DETECTION BENCHMARK RESULTS")
    print("=" * 60)

    print("\n--- Method Performance ---\n")
    print(method_df.to_string(index=False, float_format=lambda x: f"{x:.3f}" if pd.notna(x) else "N/A"))

    print("\n--- Detection Rates by Scenario ---\n")
    pivot = scenario_df.pivot(index="scenario", columns="method", values="detection_rate")
    print(pivot.to_string(float_format=lambda x: f"{x:.2f}"))

    print("\n--- Best Method by Scenario ---\n")
    for scenario in scenario_df["scenario"].unique():
        scenario_data = scenario_df[scenario_df["scenario"] == scenario]
        best = scenario_data.loc[scenario_data["detection_rate"].idxmax(), "method"]
        rate = scenario_data["detection_rate"].max()
        print(f"  {scenario}: {best} ({rate:.2f})")


if __name__ == "__main__":
    # Test evaluation on a small sample
    try:
        from .simulation import generate_all_scenarios, SimulationParams
        from .detection import run_all_methods
    except ImportError:
        from simulation import generate_all_scenarios, SimulationParams
        from detection import run_all_methods

    params = SimulationParams(n_points=120, period=12.0, noise_sd=1.0)
    series = generate_all_scenarios(n_series_per_scenario=10, params=params, seed=42)

    print(f"Running detection on {len(series)} series...")
    results = run_all_methods(series, known_period=12.0)

    print("Evaluating results...")
    evaluation = evaluate_all(results, series)

    print_summary(evaluation)
