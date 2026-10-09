"""scipy moments reference generator (family `moments`).

Writes `validation/reference/fixtures/moments/scipy_moments.json`: skew and
excess-kurtosis (both bias=False and bias=True variants) plus the SAS
bimodality coefficient, for six seeded/fixed vectors. Phase 13 (STAT-01)
asserts the product's `ts_stats` moments against this fixture.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_moments.py
"""

from __future__ import annotations

import numpy as np
from scipy import stats

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {"abs": 1e-12, "rel": 1e-12}


def bimodality_coefficient_sas(g1_bias_false: float, g2_bias_false: float, n: int) -> float:
    """SAS bimodality coefficient computed from the bias=False G1/G2 moments:

    BC = (G1^2 + 1) / (G2 + 3*(n-1)^2 / ((n-2)*(n-3)))
    """
    denom = g2_bias_false + 3.0 * (n - 1) ** 2 / ((n - 2) * (n - 3))
    return (g1_bias_false**2 + 1.0) / denom


def describe(x: np.ndarray) -> dict:
    n = int(x.shape[0])
    skew_bias_false = float(stats.skew(x, bias=False))
    skew_bias_true = float(stats.skew(x, bias=True))
    kurt_bias_false = float(stats.kurtosis(x, fisher=True, bias=False))
    kurt_bias_true = float(stats.kurtosis(x, fisher=True, bias=True))
    bc = bimodality_coefficient_sas(skew_bias_false, kurt_bias_false, n)
    return {
        "x": x,
        "n": n,
        "mean": float(np.mean(x)),
        "var_ddof1": float(np.var(x, ddof=1)),
        "skew_bias_false": skew_bias_false,
        "skew_bias_true": skew_bias_true,
        "kurtosis_excess_bias_false": kurt_bias_false,
        "kurtosis_excess_bias_true": kurt_bias_true,
        "bimodality_coefficient_sas": bc,
    }


def main() -> None:
    rng = np.random.default_rng(20261008)

    toy8 = np.array([2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0])
    x20 = np.array(
        [
            3.337, 2.674, 2.292, 3.491, 2.217, 4.765, 3.613, 3.258, 0.928,
            0.481, 1.015, 3.755, 3.21, 0.738, 3.727, 1.651, 1.653, 6.178,
            3.053, 3.501,
        ]
    )
    normal_n100 = rng.normal(10, 2, 100)
    exponential_n200 = rng.exponential(3.0, 200)
    uniform_n50 = rng.uniform(0, 1, 50)
    bimodal_n200 = np.concatenate(
        [rng.normal(-2, 0.6, 100), rng.normal(2, 0.6, 100)]
    )

    vectors = {
        "toy8": describe(toy8),
        "x20": describe(x20),
        "normal_n100": describe(normal_n100),
        "exponential_n200": describe(exponential_n200),
        "uniform_n50": describe(uniform_n50),
        "bimodal_n200": describe(bimodal_n200),
    }

    payload = {
        "tolerance": TOLERANCE,
        "parameters": {
            "rng": "numpy.random.default_rng(20261008)",
            "note": "One shared rng advanced in vector order: toy8, x20 (fixed, no draw), normal_n100, exponential_n200, uniform_n50, bimodal_n200",
        },
        "vectors": vectors,
    }

    write_fixture(
        family="moments",
        filename="scipy_moments.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy"),
        payload=payload,
    )


if __name__ == "__main__":
    main()
