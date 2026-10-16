//! D-10 Monte Carlo interval-coverage test (Phase 17 plan 17-03, FCST-09).
//!
//! Seeded (SplitMix64, seed 170010) 2,000-replication local-level
//! simulation: `y_t = l_{t-1} + e_t`, `l_t = l_{t-1} + 0.3 * e_t`, `l0 = 10`,
//! `sigma = 1`, `n = 100` train + `h = 12` test. For each replication, fit
//! `ETS(A,N,N)` and `SESOptimized` on the training window and check whether
//! the true future `l_t` path (equivalently, the realised `y` at each
//! horizon under the DGP) falls inside the 95%/80% prediction interval at
//! every step. No `rand` dependency: an in-file SplitMix64 + Box-Muller
//! generator.

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};

const N_TRAIN: usize = 100;
const H: usize = 12;
const REPS: usize = 2000;
const ALPHA: f64 = 0.3;
const L0: f64 = 10.0;
const SIGMA: f64 = 1.0;
const SEED: u64 = 170010;

/// SplitMix64: 64-bit increment `0x9E3779B97F4A7C15`, the standard
/// two-multiply mix. No external dependency.
struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }

    /// Uniform in (0, 1), excluding both endpoints (needed for Box-Muller's
    /// log()).
    fn next_uniform(&mut self) -> f64 {
        // 53 bits of mantissa precision, then nudge off the [0,1) boundary.
        let bits = self.next_u64() >> 11;
        let u = (bits as f64) / ((1u64 << 53) as f64);
        u.clamp(1e-12, 1.0 - 1e-12)
    }

    /// Standard normal via Box-Muller (one value per call; the paired value
    /// is discarded for simplicity — this test is not performance-critical).
    fn next_normal(&mut self) -> f64 {
        let u1 = self.next_uniform();
        let u2 = self.next_uniform();
        (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos()
    }
}

/// Simulate one local-level replication: returns (train values, test
/// values — the continuation of the same DGP for `h` more steps).
fn simulate_one(rng: &mut SplitMix64) -> (Vec<f64>, Vec<f64>) {
    let mut l = L0;
    let mut train = Vec::with_capacity(N_TRAIN);
    for _ in 0..N_TRAIN {
        let e = SIGMA * rng.next_normal();
        let y = l + e;
        l += ALPHA * e;
        train.push(y);
    }
    let mut test = Vec::with_capacity(H);
    for _ in 0..H {
        let e = SIGMA * rng.next_normal();
        let y = l + e;
        l += ALPHA * e;
        test.push(y);
    }
    (train, test)
}

struct CoverageResult {
    pooled_95: f64,
    per_h_95: [f64; H],
    pooled_80: f64,
    per_h_80: [f64; H],
    fit_failures: usize,
}

fn run_coverage(model: ModelType, ets_spec: Option<&str>) -> CoverageResult {
    let mut rng = SplitMix64::new(SEED);
    let mut hits_95 = [0usize; H];
    let mut hits_80 = [0usize; H];
    let mut fit_failures = 0usize;
    let mut valid_reps = 0usize;

    for _ in 0..REPS {
        let (train, test) = simulate_one(&mut rng);
        let opts: Vec<Option<f64>> = train.iter().map(|v| Some(*v)).collect();

        let mut rep_ok = true;
        for (level, hits) in [(0.95_f64, &mut hits_95), (0.80_f64, &mut hits_80)] {
            let options = ForecastOptions {
                model,
                ets_spec: ets_spec.map(|s| s.to_string()),
                horizon: H,
                confidence_level: level,
                seasonal_period: 0,
                auto_detect_seasonality: false,
                ..Default::default()
            };
            match forecast(&opts, &options) {
                Ok(out) => {
                    for h in 0..H {
                        if test[h] >= out.lower[h] && test[h] <= out.upper[h] {
                            hits[h] += 1;
                        }
                    }
                }
                Err(_) => {
                    rep_ok = false;
                }
            }
        }
        if rep_ok {
            valid_reps += 1;
        } else {
            fit_failures += 1;
        }
    }

    let denom = valid_reps.max(1) as f64;
    let mut per_h_95 = [0.0; H];
    let mut per_h_80 = [0.0; H];
    for h in 0..H {
        per_h_95[h] = hits_95[h] as f64 / denom;
        per_h_80[h] = hits_80[h] as f64 / denom;
    }
    let pooled_95 = hits_95.iter().sum::<usize>() as f64 / (denom * H as f64);
    let pooled_80 = hits_80.iter().sum::<usize>() as f64 / (denom * H as f64);

    CoverageResult {
        pooled_95,
        per_h_95,
        pooled_80,
        per_h_80,
        fit_failures,
    }
}

fn assert_coverage(label: &str, r: &CoverageResult) {
    println!(
        "{label}: fit_failures={} pooled_95={:.4} pooled_80={:.4}",
        r.fit_failures, r.pooled_95, r.pooled_80
    );
    println!(
        "{label}: per_h_95={:?}",
        r.per_h_95
            .iter()
            .map(|x| format!("{x:.4}"))
            .collect::<Vec<_>>()
    );
    println!(
        "{label}: per_h_80={:?}",
        r.per_h_80
            .iter()
            .map(|x| format!("{x:.4}"))
            .collect::<Vec<_>>()
    );

    assert_eq!(r.fit_failures, 0, "{label}: expected zero fit failures");
    assert!(
        (0.92..=0.98).contains(&r.pooled_95),
        "{label}: pooled 95% coverage {:.4} outside [0.92, 0.98]",
        r.pooled_95
    );
    for (h, p) in r.per_h_95.iter().enumerate() {
        assert!(
            (0.92..=0.98).contains(p),
            "{label}: h={h} 95% coverage {p:.4} outside [0.92, 0.98]"
        );
    }
    assert!(
        (0.77..=0.83).contains(&r.pooled_80),
        "{label}: pooled 80% coverage {:.4} outside [0.77, 0.83]",
        r.pooled_80
    );
    for (h, p) in r.per_h_80.iter().enumerate() {
        assert!(
            (0.77..=0.83).contains(p),
            "{label}: h={h} 80% coverage {p:.4} outside [0.77, 0.83]"
        );
    }
}

#[test]
fn ets_ann_local_level_coverage() {
    let r = run_coverage(ModelType::ETS, Some("ANN"));
    assert_coverage("ETS(A,N,N)", &r);
}

#[test]
fn ses_optimized_local_level_coverage() {
    let r = run_coverage(ModelType::SESOptimized, None);
    assert_coverage("SESOptimized", &r);
}
