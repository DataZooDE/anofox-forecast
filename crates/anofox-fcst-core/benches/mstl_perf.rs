//! Performance benchmark for MSTL/STL operations at scale (100k series)
//!
//! Run with: cargo bench --bench mstl_perf

use std::time::{Duration, Instant};

fn generate_seasonal_series(n: usize, periods: &[usize]) -> Vec<f64> {
    (0..n)
        .map(|i| {
            let trend = 0.01 * i as f64;
            let seasonal: f64 = periods
                .iter()
                .enumerate()
                .map(|(j, &p)| {
                    let amplitude = 10.0 / (j + 1) as f64;
                    amplitude * (2.0 * std::f64::consts::PI * i as f64 / p as f64).sin()
                })
                .sum();
            trend + seasonal + (i % 7) as f64 * 0.1 // small noise
        })
        .collect()
}

fn benchmark_fn<F, R>(name: &str, iterations: usize, mut f: F) -> Duration
where
    F: FnMut() -> R,
{
    // Warmup
    let _ = f();

    let start = Instant::now();
    for _ in 0..iterations {
        let _ = std::hint::black_box(f());
    }
    let elapsed = start.elapsed();
    let per_iter = elapsed / iterations as u32;
    println!(
        "{}: total={:?}, per_iter={:?}, iters={}",
        name, elapsed, per_iter, iterations
    );
    elapsed
}

fn main() {
    println!("=== MSTL/STL Performance Benchmark ===\n");

    // Test parameters
    let series_lengths = [100, 500, 1000, 5000, 10000];
    let periods_short = vec![12];
    let periods_multi = vec![7, 12, 52];

    println!("--- 1. MSTL Decomposition Benchmarks ---\n");

    for &n in &series_lengths {
        let values = generate_seasonal_series(n, &periods_short);
        let periods: Vec<i32> = periods_short.iter().map(|&p| p as i32).collect();

        let iters = if n <= 1000 { 100 } else { 10 };

        benchmark_fn(
            &format!("mstl_decompose(n={}, single period)", n),
            iters,
            || {
                anofox_fcst_core::mstl_decompose(
                    &values,
                    &periods,
                    anofox_fcst_core::InsufficientDataMode::Fail,
                )
            },
        );
    }

    println!();

    // Multi-period decomposition
    for &n in &series_lengths {
        let values = generate_seasonal_series(n, &periods_multi);
        let periods: Vec<i32> = periods_multi.iter().map(|&p| p as i32).collect();

        let iters = if n <= 1000 { 100 } else { 10 };

        benchmark_fn(
            &format!("mstl_decompose(n={}, 3 periods)", n),
            iters,
            || {
                anofox_fcst_core::mstl_decompose(
                    &values,
                    &periods,
                    anofox_fcst_core::InsufficientDataMode::Fail,
                )
            },
        );
    }

    println!("\n--- 2. Period Detection Benchmarks ---\n");

    let period_methods = [
        ("fft", anofox_fcst_core::PeriodMethod::Fft),
        ("acf", anofox_fcst_core::PeriodMethod::Acf),
        ("auto", anofox_fcst_core::PeriodMethod::Auto),
        ("autoperiod", anofox_fcst_core::PeriodMethod::Autoperiod),
        ("lomb_scargle", anofox_fcst_core::PeriodMethod::LombScargle),
        ("aic", anofox_fcst_core::PeriodMethod::Aic),
        ("ssa", anofox_fcst_core::PeriodMethod::Ssa),
        ("stl", anofox_fcst_core::PeriodMethod::Stl),
        (
            "matrix_profile",
            anofox_fcst_core::PeriodMethod::MatrixProfile,
        ),
        ("sazed", anofox_fcst_core::PeriodMethod::Sazed),
    ];

    for &n in &[500, 1000, 2000] {
        let values = generate_seasonal_series(n, &periods_short);

        println!("Series length: {}", n);

        for (name, method) in &period_methods {
            let iters = match *name {
                "matrix_profile" => 3, // Very slow
                "ssa" => 10,
                "stl" => 10,
                "lomb_scargle" => 20,
                "aic" => 20,
                _ => 50,
            };

            benchmark_fn(&format!("  detect_periods({})", name), iters, || {
                anofox_fcst_core::detect_periods(&values, *method, None, None)
            });
        }
        println!();
    }

    println!("\n--- 3. Scalability Test (many series) ---\n");

    // Simulate 100k series scenario: measure how long to process N series
    let series_counts = [100, 1000, 10000];
    let series_len = 100; // Short series typical in batch processing

    for &n_series in &series_counts {
        let series_batch: Vec<Vec<f64>> = (0..n_series)
            .map(|seed| {
                (0..series_len)
                    .map(|i| {
                        10.0 * (2.0 * std::f64::consts::PI * i as f64 / 12.0).sin()
                            + (seed % 100) as f64 * 0.01
                    })
                    .collect()
            })
            .collect();

        let periods: Vec<i32> = vec![12];

        println!("Processing {} series (len={} each):", n_series, series_len);

        // MSTL decomposition for all series
        benchmark_fn(&format!("  mstl_decompose x{}", n_series), 1, || {
            series_batch
                .iter()
                .map(|s| {
                    anofox_fcst_core::mstl_decompose(
                        s,
                        &periods,
                        anofox_fcst_core::InsufficientDataMode::Fail,
                    )
                })
                .collect::<Vec<_>>()
        });

        // Period detection for all series (FFT - fastest)
        benchmark_fn(&format!("  detect_periods(fft) x{}", n_series), 1, || {
            series_batch
                .iter()
                .map(|s| {
                    anofox_fcst_core::detect_periods(
                        s,
                        anofox_fcst_core::PeriodMethod::Fft,
                        None,
                        None,
                    )
                })
                .collect::<Vec<_>>()
        });

        // Forecast for all series
        benchmark_fn(&format!("  forecast(Naive) x{}", n_series), 1, || {
            series_batch
                .iter()
                .map(|s| {
                    let opts = s.iter().map(|&v| Some(v)).collect::<Vec<_>>();
                    anofox_fcst_core::forecast(
                        &opts,
                        &anofox_fcst_core::ForecastOptions {
                            model: anofox_fcst_core::ModelType::Naive,
                            horizon: 12,
                            ..Default::default()
                        },
                    )
                })
                .collect::<Vec<_>>()
        });

        benchmark_fn(&format!("  forecast(MSTL) x{}", n_series), 1, || {
            series_batch
                .iter()
                .map(|s| {
                    let opts = s.iter().map(|&v| Some(v)).collect::<Vec<_>>();
                    anofox_fcst_core::forecast(
                        &opts,
                        &anofox_fcst_core::ForecastOptions {
                            model: anofox_fcst_core::ModelType::MSTL,
                            horizon: 12,
                            seasonal_period: 12,
                            auto_detect_seasonality: false,
                            ..Default::default()
                        },
                    )
                })
                .collect::<Vec<_>>()
        });

        println!();
    }

    println!("\n--- 4. Memory Allocation Analysis ---\n");

    // Check memory pressure for large series
    let large_n = 50000;
    let values = generate_seasonal_series(large_n, &[12, 52, 365]);
    let periods: Vec<i32> = vec![12, 52, 365];

    println!("Large series (n={}):", large_n);

    benchmark_fn("  mstl_decompose (3 periods)", 3, || {
        anofox_fcst_core::mstl_decompose(
            &values,
            &periods,
            anofox_fcst_core::InsufficientDataMode::Fail,
        )
    });

    // This will be slow - shows the bottleneck
    println!("\nSlow methods on large series:");
    benchmark_fn("  detect_periods(stl)", 1, || {
        anofox_fcst_core::detect_periods(&values, anofox_fcst_core::PeriodMethod::Stl, None, None)
    });

    // Matrix profile is O(n^2) - will be very slow
    let medium_n = 2000;
    let medium_values = generate_seasonal_series(medium_n, &[12]);
    benchmark_fn(
        &format!("  detect_periods(matrix_profile, n={})", medium_n),
        1,
        || {
            anofox_fcst_core::detect_periods(
                &medium_values,
                anofox_fcst_core::PeriodMethod::MatrixProfile,
                None,
                None,
            )
        },
    );

    println!("\n=== Benchmark Complete ===");
}
