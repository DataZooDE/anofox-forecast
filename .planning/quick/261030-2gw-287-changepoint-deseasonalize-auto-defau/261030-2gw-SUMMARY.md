---
quick_id: 261030-2gw
status: complete
issue: 287
commits: [045486fa]
---

# 261030-2gw: changepoint deseasonalize 'auto' default (#287)

## Change
`deseasonalize` now defaults to `auto`. It deseasonalizes a series only when all of these hold:
- there is a usable period: an explicit `seasonal_period`, or an FFT-detected period with confidence ≥ 10;
- the series spans at least 3 cycles;
- the STL seasonal strength is ≥ 0.5.

Otherwise the raw path runs, byte-identical to `false`. `true` and `false` are unchanged.

## Gate calibration
200 simulated series per type, n = 60..365. Share of series the gate deseasonalizes:

| Series | Strength ≥ 0.3 (classify default) | Final gate (≥ 0.5, 3 cycles, confidence ≥ 10) |
|---|---:|---:|
| Noise | 12% | 0% |
| Random walk | 63% | 6% |
| Trend | 8.5% | 0% |
| Level step | 24% | 0% |
| Weak seasonal | — | 94% |
| Strong seasonal | 100% | 100% |

## Verification
- The #287 panel's default now flags only B on 2024-02-20, on both `_by` and `_agg`.
- `'false'` reproduces the old md5 (`083042b0…`).
- An invalid value throws with "must be true/false/1/0/auto".
- Full sqllogictest suite: 6989 assertions in 110 test cases, all passing.
- Rust: 807 tests passing. fmt is clean; clippy shows 0 warnings, the same as the baseline.
- Docs snippets: 369 passing, 0 failing.
