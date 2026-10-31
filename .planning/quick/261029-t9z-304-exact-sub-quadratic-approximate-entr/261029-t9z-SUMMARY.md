---
quick_id: 261029-t9z
status: complete
issue: 304
commits: [890be302, 1894964a, 09d40ac9]
---

# 261029-t9z: exact sub-quadratic approximate entropy (#304)

Executed inline (no sub-agents). The plan's three tasks were done as one code commit plus a CHANGELOG commit.

## What changed
- `crates/anofox-fcst-core/src/template_match.rs` (new): rank-space template-match counting.
  - m=2 uses a Fenwick sweep with counting-sorted points and corner queries.
  - m=3 uses CDQ divide-and-conquer with memory-bounded corner-family passes (an 8M-item cap) and a pairwise leaf below 32 items.
  - The coordinate-0 interval uses the band search's own predicate (`v >= a - r && v <= a + r && |a-v| <= r`). Later coordinates use `|a-v| <= r`. This keeps the counts bit-identical.
- `stats.rs`: the original counter is renamed `template_match_counts_band`, with its body unchanged. `template_match_counts` dispatches to the fast path for m in {2,3}, finite x and r, and n_m at or above the measured thresholds (1,500 for m=2, 8,000 for m=3).

## Verification
- 9 equivalence tests compare against the band search with `assert_eq` on the counts. They cover:
  - random data and integer ties at distance exactly r
  - repeats, signed zeros, constants, r=0
  - near-boundary ulp nudges, frequent-minimum (skipped low corners) and pass-chunking cases
  - the rounding-asymmetry fixture, which is live (band != abs-only)
  - the dispatcher
- `cargo test -p anofox-fcst-core`: all green. That includes the R pracma pins (`tests/stats_reference.rs`) and the tsfresh entropy pins.
- `cargo fmt --check` is clean. Clippy: 0 warnings before, 0 after.
- Rebuilt extension (`DUCKDB_VERSION=v2.0.0 make release`): the probe diff (`printf %.17g` of ts_stats entropy and ts_features sample/approximate entropy, n = 2k/10k/40k, 2 series) is **empty**.
- sqllogictests: ts_stats* 193/3, ts_features* 187/3, ts_agg_window 21/1. All pass.

## Timings (release)
| | before | after |
|---|---:|---:|
| ApEn counts, 40k, m=2 (Rust) | 270 ms | 22 ms |
| ApEn counts, 40k, m=3 (Rust) | 354 ms | 78 ms |
| `approximate_entropy`, 160k (Rust) | ~44 s | 0.46 s |
| `ts_stats_agg` 10k (SQL) | 0.19 s | 0.20 s |
| `ts_stats_agg` 40k (SQL) | 2.52 s | 1.63 s |
| `ts_stats_agg` 160k (SQL) | 44.5 s | 24.1 s |

## Follow-up in the same task (user approved last-bit drift): 09d40ac9
A profile at 160k rows after the entropy fix showed two other quadratic parts: `detect_seasonality` took 21.4 s (direct ACF at every lag up to n/2), and `compute_stability` took 2.2 s (naive rolling means with window n/5).
- **ACF by FFT** (Wiener-Khinchin) when `n * max_lag >= 4096 * 2048`. It agrees with direct sums to < 1e-11 on 5 test series, including a square wave with flat ACF runs. Peak comparisons in the FFT path use a 1e-10 tie tolerance. The detected periods are identical to the direct path in the test.
- **Stability** uses a Neumaier-compensated sliding window sum from 4,096 rows. Means agree to 1e-12 relative and stability to 1e-9.
- Below these sizes the original code runs unchanged, so existing pins are bit-identical.

## Final timings
| | before | after |
|---|---:|---:|
| `ts_stats_agg` 10k (SQL) | 0.19 s | 0.055 s |
| `ts_stats_agg` 40k (SQL) | 2.52 s | 0.21 s |
| `ts_stats_agg` 160k (SQL) | 44.5 s | 0.89 s |
| `ts_stats_agg` 1M (SQL) | not run (quadratic extrapolation ~20 min) | 6.5 s, 409 MB peak RSS |
| `compute_ts_stats` 160k (Rust, ignored perf gate < 3 s) | ~44 s | 0.87 s |

Verification after the follow-up:
- the entropy probe diff is still empty
- `cargo test -p anofox-fcst-core`: 679 passed, 0 failed
- fmt and clippy are clean
- the full sqllogictest suite passes: 6962 assertions in 109 test cases
