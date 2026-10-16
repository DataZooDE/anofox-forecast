//! 15,000-row exogenous-fit peak-heap guard (DEP-01 memory proof).
//!
//! Own integration-test binary (installs a global allocator) with exactly
//! one test function so no concurrent test pollutes the peak measurement.
//!
//! Historical context: before anofox-regression 0.5.14, this crate's own
//! wrapper `fit_ols_regression` (deleted in plan 10-04, D-10) materialised an
//! n×n matrix for its OLS fit — ~1.7 GB at 15,330 rows. Since plan 10-04,
//! `forecast_with_exog`'s OptimizedTheta path no longer runs that wrapper at
//! all: it attaches the regressor to the crate `TimeSeries` via
//! `CalendarAnnotations` and lets `anofox-forecast`'s own exog support fit
//! (its internal pre-regression uses Cholesky normal equations over a k×k
//! system — `utils/ols.rs::ols_fit` — not an n×n materialisation). This test
//! keeps the SAME named, documented 256 MiB limit as a regression guard: if
//! the crate's own exog path ever regresses back to O(n²) memory, this test
//! catches it exactly as it caught anofox-regression's pre-0.5.15 behaviour.

mod common;

use anofox_fcst_core::forecast::{
    forecast_with_exog, ExogenousData, ForecastOptionsExog, ModelType,
};
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

/// Tracks live (`CURRENT`) and high-water-mark (`PEAK`) bytes allocated
/// through this process's global allocator. Delegates every operation to
/// `std::alloc::System` — this instruments, it does not replace, the
/// system allocator.
struct PeakAlloc;

static CURRENT: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for PeakAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let ptr = System.alloc(layout);
        if !ptr.is_null() {
            let cur = CURRENT.fetch_add(layout.size(), Ordering::SeqCst) + layout.size();
            PEAK.fetch_max(cur, Ordering::SeqCst);
        }
        ptr
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        System.dealloc(ptr, layout);
        CURRENT.fetch_sub(layout.size(), Ordering::SeqCst);
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let ptr = System.alloc_zeroed(layout);
        if !ptr.is_null() {
            let cur = CURRENT.fetch_add(layout.size(), Ordering::SeqCst) + layout.size();
            PEAK.fetch_max(cur, Ordering::SeqCst);
        }
        ptr
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        let new_ptr = System.realloc(ptr, layout, new_size);
        if !new_ptr.is_null() {
            if new_size >= layout.size() {
                let delta = new_size - layout.size();
                let cur = CURRENT.fetch_add(delta, Ordering::SeqCst) + delta;
                PEAK.fetch_max(cur, Ordering::SeqCst);
            } else {
                let delta = layout.size() - new_size;
                CURRENT.fetch_sub(delta, Ordering::SeqCst);
            }
        }
        new_ptr
    }
}

#[global_allocator]
static ALLOCATOR: PeakAlloc = PeakAlloc;

/// anofox-regression 0.5.3 materialised an n×n matrix (~1.7 GB at 15,330
/// rows) for this fit; 0.5.15 needs ~11 MB. 256 MiB is a generous margin
/// between those two regimes — comfortably above the new behaviour,
/// comfortably below the old one.
const PEAK_HEAP_LIMIT_BYTES: usize = 256 * 1024 * 1024;

const SELF_CHECK_PROBE_BYTES: usize = 64 * 1024 * 1024;

#[test]
fn exog_fit_15k_rows_has_bounded_peak_heap() {
    // (a) Instrument self-check: a known-size allocation must raise PEAK
    // by at least its own size, proving the allocator is actually wired
    // up (not silently bypassed by a different global allocator).
    let baseline = CURRENT.load(Ordering::SeqCst);
    PEAK.store(baseline, Ordering::SeqCst);
    {
        let probe: Vec<u8> = std::hint::black_box(vec![0u8; SELF_CHECK_PROBE_BYTES]);
        drop(std::hint::black_box(probe));
    }
    let probe_peak = PEAK.load(Ordering::SeqCst);
    let probe_delta = probe_peak.saturating_sub(baseline);
    assert!(
        probe_delta >= SELF_CHECK_PROBE_BYTES,
        "instrument self-check failed: peak delta {probe_delta} bytes < {SELF_CHECK_PROBE_BYTES} \
         bytes (global allocator not tracking as expected)"
    );

    // (b) Build the 15,330-row tiled exogenous design from the DEP-01
    // fixture's exog_typical case. Tiling the 365-row design 42x leaves
    // the OLS coefficients unchanged (X^T X and X^T y both scale by 42),
    // so the fixture's `coef` is still the expected regression effect.
    let fx = common::load_fixture("multiscale_ols/cases.json");
    let case = &fx["cases"]["exog_typical"];
    let x_cols: Vec<Vec<f64>> = case["X"]
        .as_array()
        .expect("exog_typical: 'X' is not an array")
        .iter()
        .map(common::f64_vec)
        .collect();
    let y = common::f64_vec(&case["y"]);
    let coef = common::f64_vec(&case["coef"]);
    let sql_point_abs = common::tolerance(&fx, "sql_point_abs");

    const TILES: usize = 42;
    let historical: Vec<Vec<f64>> = x_cols
        .iter()
        .map(|col| {
            col.iter()
                .copied()
                .cycle()
                .take(col.len() * TILES)
                .collect()
        })
        .collect();
    let y_tiled: Vec<f64> = y.iter().copied().cycle().take(y.len() * TILES).collect();
    assert_eq!(
        y_tiled.len(),
        15330,
        "tiled series should have 365 * 42 = 15330 rows"
    );

    let future: Vec<Vec<f64>> = x_cols.iter().map(|col| col[0..5].to_vec()).collect();

    let options = ForecastOptionsExog {
        // OptimizedTheta isolates the regression effect: the crate's own
        // exog-aware fit pre-regresses the near-white-noise residual series
        // (effect ~ 0 from OptimizedTheta itself), so the combined point
        // forecast is dominated by the regressor's own coefficient — close
        // to the fixture's `coef`-implied projection within sql_point_abs.
        model: ModelType::OptimizedTheta,
        horizon: 5,
        seasonal_period: 0,
        auto_detect_seasonality: false,
        exog: Some(ExogenousData::new(historical, future.clone())),
        ..Default::default()
    };

    let input: Vec<Option<f64>> = y_tiled.iter().copied().map(Some).collect();

    // (c) Measure the peak heap delta of the fit+forecast call itself.
    let baseline2 = CURRENT.load(Ordering::SeqCst);
    PEAK.store(baseline2, Ordering::SeqCst);

    let output = forecast_with_exog(&input, &options)
        .expect("forecast_with_exog on the 15,330-row tiled exogenous design");

    let peak_after = PEAK.load(Ordering::SeqCst);
    let delta = peak_after.saturating_sub(baseline2);
    eprintln!(
        "exog_fit_15k_rows_has_bounded_peak_heap: peak heap delta = {delta} bytes ({:.2} MiB), \
         limit = {PEAK_HEAP_LIMIT_BYTES} bytes ({:.2} MiB)",
        delta as f64 / (1024.0 * 1024.0),
        PEAK_HEAP_LIMIT_BYTES as f64 / (1024.0 * 1024.0)
    );

    assert!(
        delta < PEAK_HEAP_LIMIT_BYTES,
        "peak heap delta {delta} bytes >= limit {PEAK_HEAP_LIMIT_BYTES} bytes \
         (anofox-regression regression O(n^2) memory may have returned)"
    );

    // (d) 5 finite points, each within sql_point_abs of [1, x_i] . coef.
    assert_eq!(output.point.len(), 5, "expected horizon=5 points");
    for (i, p) in output.point.iter().enumerate() {
        assert!(p.is_finite(), "point[{i}] is not finite: {p}");
        let mut want = coef[0];
        for (j, col) in future.iter().enumerate() {
            want += coef[j + 1] * col[i];
        }
        common::assert_close(*p, want, sql_point_abs, 0.0, &format!("point[{i}]"));
    }
}
