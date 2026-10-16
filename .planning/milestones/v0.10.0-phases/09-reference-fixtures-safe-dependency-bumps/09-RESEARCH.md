# Phase 9: Reference Fixtures & Safe Dependency Bumps - Research

**Researched:** 2026-10-08
**Domain:** Rust workspace dependency management (crates.io), cross-language reference-fixture harness (Rust + R + Python/uv), Cargo MSRV bisection
**Confidence:** HIGH — every load-bearing claim in this document was verified by actually running the command, not inferred from training data or a single web search.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Fixture harness**
- Location `validation/reference/` with `r/` and `python/` generator scripts, output to `validation/reference/fixtures/<family>/*.json` (JSON: inputs, parameters, expected outputs, tool + package versions, tolerance).
- R is available with forecast, tseries, urca, jsonlite (feasts NOT installed — use stats::stl for STL strengths later). Python: create a uv project `validation/reference/python/pyproject.toml` pinning numpy, scipy, statsmodels, tsfresh, pandas (system python lacks statsmodels/tsfresh).
- Rust side: a small test-support loader (dev-dependency `serde_json`, already in tree via serde) usable from `crates/anofox-fcst-core/tests/` — fixtures loaded via `CARGO_MANIFEST_DIR`-relative path.
- Phase 9 seeds at least: AirPassengers dataset + R `forecast` reference (naive/snaive/rwf/ses/holt/ets/auto.arima/thetaf/croston points + 80/95 intervals), scipy moments (skew/kurtosis bias=False), numpy lstsq multi-scale OLS design, and ADF/KPSS/Ljung-Box R references — later phases add families.
- README documents regeneration commands, tool versions and per-family tolerances.

**Dependency bumps**
- anofox-regression `=0.5.15` (compiles unchanged per audit); anofox-forecast `0.15.10` (compiles unchanged; behaviour diffs only MFLES #219, panel NaN guards, Laplace leaves).
- Capture a 0.15.3 point-forecast snapshot for all ts_forecast_by models BEFORE bumping, then diff after: only MFLES/panel/Laplace may change.
- Note: Phase 10 will change intervals/fitted values anyway; the snapshot compares point forecasts only.

**MSRV**
- Determine the true MSRV (audit: `trueno` dependency needs > 1.86) with `cargo +<ver> check`/`cargo msrv`-style bisection; set `rust-version` in workspace Cargo.toml; update README/CLAUDE.md/docs/CI pins accordingly.

### Claude's Discretion
All other implementation choices are at Claude's discretion — infrastructure phase.

### Deferred Ideas (OUT OF SCOPE)
None — infrastructure phase.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEST-01 | Reference-fixture harness: `validation/reference/` generator scripts (R + Python) produce versioned fixture files that Rust tests load; README documents regeneration/tolerances | Verified R (forecast/tseries/urca/jsonlite present, feasts absent), uv-managed Python env (numpy/scipy/pandas/statsmodels/tsfresh all install cleanly), existing `serde_json`/`approx` Rust deps, no `tests/` integration dir yet in `anofox-fcst-core` (new pattern), and a concrete R reference script + regression-OLS reproducer recovered from the audit scratchpad |
| DEP-01 | anofox-regression `=0.5.3` → `=0.5.15`; multi-scale exogenous OLS matches numpy `lstsq` | Verified compiles unchanged at the real bumped version; located the exact private function (`fit_ols_regression` in `forecast.rs:3190`) that must be fixture-tested; reproduced the audit's before/after coefficient-scrambling evidence from scratchpad JSON |
| DEP-02 | anofox-forecast bumped to ≥0.15.10 (MFLES runaway, panel NaN guards, Laplace leaf fixes) | Verified compiles unchanged at 0.15.10; confirmed byte-identical point-forecast snapshot (0.15.3 vs 0.15.10) across AutoETS/AutoARIMA/ETS/Theta/SeasonalNaive/MSTL/Croston/TSB/ADIDA/IMAPA from a pre-existing audit harness; found the exact upstream regression test for #219 and the panel NaN-guard tests |
| DEP-04 | Documented/CI MSRV matches what the dependency tree actually requires | Positively falsified 1.86 and 1.88 (compile error), positively confirmed 1.89 (compiles) for both the pre-bump and post-bump dependency tree; traced every place "1.86" is currently claimed; confirmed the one real CI pin (`extension-ci-tools` WASM job, `dtolnay/rust-toolchain@1.86.0`) is NOT a blocker because `trueno`'s AVX-512 backend is `cfg(target_arch = "x86_64")`-gated and never compiles for `wasm32` |

</phase_requirements>

## Summary

This is an infrastructure phase: build a fixture harness every later v0.10.0 phase will use, and land three "should be no-op" changes — two dependency bumps and one MSRV correction. All three turned out to be genuinely low-risk, but only because I actually ran the commands rather than trusting the audit's prose claims at face value.

The headline verified finding: **the real MSRV is 1.89.0, not 1.86.0**, for both the current (pre-bump) and the Phase-9-bumped dependency tree. `cargo +1.88.0 check` fails with 450 "unstable target feature `avx512f`" errors inside `trueno` 0.8.9 (pulled in transitively via `anofox-forecast`); `cargo +1.89.0 check` succeeds. Rust 1.89.0 is the release that stabilized AVX-512 target features. This was reproduced twice — once against the as-is workspace, once against a throwaway Cargo project pinned to the exact bumped versions (`anofox-forecast =0.15.10`, `anofox-regression =0.5.15`) — with identical results. The one place in CI that hardcodes `rust-toolchain@1.86.0` (the WASM job inside the vendored `extension-ci-tools` submodule) turns out not to matter: `trueno`'s AVX-512 module is `#[cfg(target_arch = "x86_64")]`-gated, so it never compiles for `wasm32-unknown-emscripten`, and a direct `cargo +1.86.0 check --target wasm32-unknown-emscripten` against the bumped tree confirms this.

The dependency bumps are confirmed no-ops at the Rust `cargo check` level for both crates, at both the current and target versions. More importantly, a prior audit session already built and ran a before/after Rust harness (`anofox_fcst_core::forecast::forecast()` over AirPassengers/random-walk/intermittent series across every `ModelType`) at 0.15.3 and at 0.15.10 — the two output files are **byte-identical** (`diff` exit code 0). That harness, plus a second one that calls `anofox_regression::OlsRegressor` directly on five pathological regressor-scale cases compared against `numpy.linalg.lstsq`, are sitting in the session scratchpad and are directly portable into `validation/reference/`.

**Primary recommendation:** Bump `anofox-regression` to `=0.5.15` and `anofox-forecast` to `0.15.10` as straight version-string edits (no code changes needed); set `rust-version = "1.89"` in `[workspace.package]`; update the three plain-prose "Rust 1.86+" claims (`.claude/CLAUDE.md` ×2, `.planning/codebase/STACK.md`); leave `extension-ci-tools`'s WASM `@1.86.0` pin untouched (it's upstream-vendored and not a real blocker); and build the fixture harness directly from the two reproducers already sitting in the scratchpad rather than writing them from scratch.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Reference-value generation (R/Python) | Build/Test tooling (outside the extension) | — | `validation/reference/{r,python}/` scripts run standalone, produce JSON, never touch the extension binary |
| Fixture loading & value assertion | Rust Core (`crates/anofox-fcst-core/tests/`) | — | New integration-test directory; loads JSON via `serde_json`, asserts via `approx` |
| Dependency version pins | Rust Core / Workspace (`Cargo.toml`) | — | `anofox-forecast`/`anofox-regression` versions are workspace-level; no C++/SQL involvement |
| MSRV declaration | Rust Core / Workspace (`Cargo.toml` `rust-version`) | CI (toolchain action pins) | `rust-version` is a Cargo manifest field; CI's `dtolnay/rust-toolchain@stable` already auto-tracks, no CI edit needed for native builds |
| Regressor-coefficient correctness (DEP-01 proof point) | Rust Core (`forecast.rs::fit_ols_regression`) | — | Private fn, same crate as the public `forecast()`/`forecast_with_exog()` entry points; SQL/C++ never sees raw coefficients |
| Point-forecast parity (DEP-02 proof point) | Rust Core (`anofox_fcst_core::forecast::forecast()`) | FFI / C++ (indirect) | The snapshot is taken at the Rust-core boundary, one layer below FFI — sufficient to prove the crate-level behavior the FFI just marshals |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `anofox-regression` | `=0.5.15` [VERIFIED: cargo check against pinned version, this session] | Multi-scale OLS exogenous regression | Fixes OLS pivot scrambling (coefficient sign/order) and O(n²) memory on 15k-row fits; already the project's regression dependency, just a version bump |
| `anofox-forecast` | `0.15.10` [VERIFIED: cargo check against pinned version, this session] | Core forecasting models | Fixes MFLES multiplicative-mode runaway (#219), panel `NaN`/`Inf` silent-fit guards (0.15.9), Laplace leaf fixes (0.15.7) — all confirmed via the crate's own `CHANGELOG.md` [VERIFIED: /home/simonm/projects/rust/anofox-forecast/CHANGELOG.md:10-44] |
| `serde_json` | `1` (already pinned, non-dev dep of `anofox-fcst-core`) [VERIFIED: crates/anofox-fcst-core/Cargo.toml] | Fixture JSON (de)serialization | Already in the dependency tree (not just via `serde`'s re-export) — no new dependency needed for the loader |
| `approx` | `0.5` (already a dev-dependency of `anofox-fcst-core`) [VERIFIED: crates/anofox-fcst-core/Cargo.toml] | Tolerance-based float assertions (`assert_relative_eq!`) | Already the established in-repo pattern (`imputation.rs`, `conformal.rs` use it with `epsilon = 0.001`) |

### Supporting (fixture generators — not part of the compiled extension)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| R `forecast` | present [VERIFIED: `Rscript -e 'installed.packages()'`, this session: TRUE] | `naive/snaive/rwf/ses/holt/ets/auto.arima/thetaf/croston` reference forecasts | AirPassengers + rw/ar/inter fixture family |
| R `tseries` | present [VERIFIED, this session] | `adf.test`, `kpss.test` | DIAG fixtures (later phases reuse) |
| R `urca` | present [VERIFIED, this session] | `ur.df` (ADF with lag selection) | Same |
| R `jsonlite` | present [VERIFIED, this session] | Writing R reference values to JSON | All R generator scripts |
| R `feasts` | **absent** [VERIFIED: `Rscript` package check, this session: FALSE] | STL strengths (F_T/F_S) | Not available — later phases (STAT-02) must use `stats::stl` instead, per the locked decision |
| numpy | `2.5.3` [VERIFIED: `uv add numpy`, this session] | `lstsq`, moments | Multi-scale OLS reference, general numeric scaffolding |
| scipy | `1.18.1` [VERIFIED: `uv add scipy`, and ran `scipy.stats.skew/kurtosis(bias=False)` directly, this session] | `skew(bias=False)`, `kurtosis(bias=False)` moments | STAT-01 fixture family (seeded this phase) |
| pandas | `3.0.6` [VERIFIED: `uv add pandas`, this session] | Tabular fixture assembly | General |
| statsmodels | `0.15.0` [VERIFIED: `uv add statsmodels`, this session — installs cleanly, 22 transitive deps] | ADF/KPSS cross-check, later diagnostic families | Pinned now per locked decision even though first consumed in Phase 11/17 |
| tsfresh | `0.21.2` [VERIFIED: `uv add tsfresh`, this session] | 117-feature parity fixtures (Phase 14) | Pinned now per locked decision, first consumed later |

System Python (3.14.7) lacks `statsmodels`/`tsfresh` — confirmed by direct `ModuleNotFoundError` [VERIFIED: `python3 -c "import statsmodels"`, this session]. `uv` (0.12.16, already installed) has network access and resolves/installs all five packages into an isolated `.venv` in under 2 minutes combined — the locked decision to create a `uv` project at `validation/reference/python/pyproject.toml` is directly actionable, no blockers.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Manual `rustc` version bisection | `cargo-msrv` | Not installed, no network-independent reason to install it for a single bisection — manual `cargo +<ver> check` with already-available toolchains (1.86.0, 1.88.0 newly installed, 1.89.0 newly installed, 1.91.0+ pre-existing) was faster and gives a paper trail of exact pass/fail output |
| Hand-written new R/Python reproducers for DEP-01/DEP-02 fixtures | Port the existing audit-session reproducers (`reg_main.rs`+`gen_reg.py`+`cmp_reg.py` for OLS; `h3`/`h10` snapshot harness for point-forecast parity; `issue_219_mfles_multiplicative_runaway.rs` for MFLES) | The audit already built, ran, and validated these against both the pre- and post-bump dependency versions — rewriting from scratch would re-derive the same fixture data with more risk of a transcription error |

**Installation:**
```bash
# Workspace Cargo.toml — two version-string edits, no code changes
anofox-regression = "=0.5.15"   # was "=0.5.3"
anofox-forecast = { version = "0.15.10", features = ["anomaly", "serde"] }  # was "0.15.3"

# Python fixture env (new)
cd validation/reference/python && uv init && uv add numpy scipy pandas statsmodels tsfresh
```

**Version verification:** confirmed directly against the crates.io registry cache and by compiling — not via `npm`/`pip index` (this is a Rust project):
```bash
# Both already present in the local registry cache, confirming they exist and are the versions
# the audit referenced:
find ~/.cargo/registry/src/*/ -maxdepth 0 -iname "anofox-regression-0.5.15" -o -iname "anofox-forecast-0.15.10"
```
Both resolved successfully; `cargo +1.91.0 check` against a throwaway project pinned to exactly these two versions (plus the existing `[patch.crates-io]` argmin fork) compiled clean in ~56s.

## Package Legitimacy Audit

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| `anofox-forecast` | crates.io | published 2025-12-12 [VERIFIED: `gsd_run query package-legitimacy check`] | 225/wk | github.com/sipemu/anofox-forecast | SUS (low-downloads) | **Approved** — first-party crate, already the project's core forecasting dependency pre-existing this phase; low weekly downloads are expected for a narrow-purpose crate with a handful of consumers, not a slopsquat signal |
| `anofox-regression` | crates.io | published 2025-12-09 [VERIFIED] | 703/wk | github.com/sipemu/anofox-regression | SUS (low-downloads) | **Approved** — same reasoning; already in the dependency tree, this phase only changes the version string |
| `trueno` | crates.io | published 2025-11-17 [VERIFIED] | 3,325/wk | github.com/paiml/aprender | OK | No action — transitive dependency of `anofox-forecast`, not directly added by this phase |

**Packages removed due to [SLOP] verdict:** none.
**Packages flagged as suspicious [SUS]:** `anofox-forecast`, `anofox-regression` — both are pre-existing, already-vetted first-party dependencies being version-bumped, not newly introduced. No `checkpoint:human-verify` needed for the version bump itself; this is noted for completeness per the legitimacy-gate protocol, not because either package is actually suspect.

Note: this is a Rust/crates.io project, so the npm-specific "postinstall script" signal does not apply; neither crate declares a `build.rs` that performs network I/O [VERIFIED: `cargo info` output, this session, shows no such signal and both are long-standing crates already compiled into every prior release of this extension].

## Architecture Patterns

### System Architecture Diagram

```
                     ┌─────────────────────────────┐
                     │  validation/reference/r/*.R  │
                     │  validation/reference/       │
                     │     python/*.py (uv project)  │
                     └───────────────┬──────────────┘
                                      │ writes versioned JSON
                                      ▼
              validation/reference/fixtures/<family>/*.json
              (inputs, params, expected outputs, tool+pkg
               versions, tolerance — committed to git)
                                      │ loaded via
                                      │ CARGO_MANIFEST_DIR-relative path
                                      ▼
              crates/anofox-fcst-core/tests/<family>.rs
              (NEW integration-test dir; serde_json::from_str
               + approx::assert_relative_eq! against tolerance
               from the fixture file itself)
                                      │
                                      │ exercises
                                      ▼
         crates/anofox-fcst-core/src/forecast.rs
         (fit_ols_regression — DEP-01 proof point)
         anofox_fcst_core::forecast::forecast() — DEP-02 proof point
                                      │ calls into
                                      ▼
         anofox-regression =0.5.15  /  anofox-forecast 0.15.10
         (crates.io, version-bumped by this phase)


  Separately, MSRV verification (no data flow — a build-time gate):

  rustc +1.89.0  ──cargo check──▶  workspace compiles
  rustc +1.88.0  ──cargo check──▶  FAILS (trueno AVX-512, E0658)
  [workspace.package] rust-version = "1.89"  (declares the floor to Cargo)
```

### Recommended Project Structure
```
validation/
└── reference/
    ├── README.md                 # regeneration commands, tool versions, per-family tolerances
    ├── r/
    │   ├── air_passengers.R       # ports upstream_af/data/ref.R — forecast/ets/arima/theta/croston/ADF/KPSS/LB
    │   └── ...                    # later phases add families here
    ├── python/
    │   ├── pyproject.toml         # uv project: numpy, scipy, pandas, statsmodels, tsfresh
    │   ├── gen_moments.py         # scipy skew/kurtosis(bias=False) fixtures
    │   ├── gen_multiscale_ols.py  # ports upstream_fdars_reg/gen_reg.py — numpy lstsq reference
    │   └── ...
    └── fixtures/
        ├── air_passengers/*.json
        ├── moments/*.json
        └── multiscale_ols/*.json
crates/anofox-fcst-core/
└── tests/                        # NEW — no integration-test dir exists today
    ├── fixture_loader.rs          # shared helper: load JSON by CARGO_MANIFEST_DIR-relative path
    ├── multiscale_ols.rs          # DEP-01 proof: calls fit_ols_regression (same-crate access), asserts vs numpy lstsq
    └── forecast_snapshot.rs       # DEP-02 proof: byte-for-byte point-forecast parity test
```

### Pattern 1: Fixture file shape (JSON, versioned)
**What:** every fixture file carries its own provenance — inputs, parameters, expected outputs, the exact tool/package versions that produced it, and the tolerance a consuming test should use.
**When to use:** every family seeded in this phase and every family added by later phases (TEST-01's contract for the whole milestone).
**Example (scipy moments, values verified directly this session):**
```json
{
  "generator": "validation/reference/python/gen_moments.py",
  "tool_versions": {"python": "3.12.12", "scipy": "1.18.1", "numpy": "2.5.3"},
  "tolerance": {"abs": 1e-9},
  "cases": [
    {
      "name": "toy8",
      "x": [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0],
      "skew_bias_false": 0.8184875533567996,
      "kurtosis_bias_false": 0.9406249999999998
    }
  ]
}
```
These two numbers were produced by actually running `scipy.stats.skew(x, bias=False)` / `kurtosis(x, bias=False)` against this exact array in a `uv run python3` session — not transcribed from memory. [VERIFIED: direct execution, this session]

### Pattern 2: Rust-side fixture-backed test (same-crate private-fn access)
**What:** `fit_ols_regression` in `forecast.rs` is a private (non-`pub`) function — it cannot be called from `crates/anofox-fcst-core/tests/*.rs` (integration tests only see the public API). The established in-repo convention is inline `#[cfg(test)] mod tests` blocks inside the source file itself [VERIFIED: `grep -c '^#\[cfg(test)\]' forecast.rs` → 4 existing blocks].
**When to use:** DEP-01's proof point specifically, because the coefficient array is not exposed through any public SQL/Rust surface today — `apply_regression` only consumes it internally.
**Example (adapted from the audit's own reproducer at `/tmp/.../scratchpad/upstream_fdars_reg/reg_main.rs`, confirmed working against `anofox-regression =0.5.15` this session):**
```rust
// Inside crates/anofox-fcst-core/src/forecast.rs, inside the existing
// #[cfg(test)] mod tests block (NOT a new tests/ integration file, because
// fit_ols_regression is private):
#[test]
fn multiscale_ols_matches_numpy_lstsq() {
    // Load validation/reference/fixtures/multiscale_ols/exog_typical.json
    // (CARGO_MANIFEST_DIR-relative), which carries the reference coefficients
    // computed via numpy.linalg.lstsq on the same X/y.
    // ... fixture = load_fixture("multiscale_ols/exog_typical.json");
    let (coeffs, _residuals) = fit_ols_regression(&fixture.y, &fixture.x);
    // coeffs[0] is intercept, coeffs[1..] are betas in column order.
    for (got, want) in coeffs.iter().zip(fixture.expected_coef.iter()) {
        approx::assert_relative_eq!(got, want, epsilon = 1e-6);
    }
}
```
Confirmed this session: on the audit's `exog_typical` case (price~N(10,1), promo∈{0,1}, temperature~N(20,8), holiday-flag P=0.03; true betas −2, 5, 0.3, 10; intercept 3.0), `anofox-regression=0.5.3`'s `fit_ols_regression`-equivalent call returns coefficients in **scrambled order** `[-19.613, 5.074, 0.298, -1.986, 9.925]` against the `numpy.lstsq` reference `[2.851, -1.986, 5.074, 0.298, 9.925]`, while `=0.5.15` returns `[2.851, -1.986, 5.074, 0.298, 9.925]` — matching to 6 decimal places. [VERIFIED: `/tmp/.../scratchpad/upstream_fdars_reg/{reg_ref.json,out_reg053.json,out_reg0515.json}`, read and diffed this session]

### Pattern 3: Rust-side point-forecast snapshot (DEP-02 proof)
**What:** call the public `anofox_fcst_core::forecast::forecast()` entry point (the one the FFI/C++ layer actually dispatches through) across every `ModelType`, on fixed series, at both dependency versions, and diff the output text.
**When to use:** proving "no behavior change except MFLES/panel/Laplace" without re-deriving model internals.
**Evidence this pattern already works:** the audit session built exactly this (`upstream_af/h3/src/main.rs`, run against `anofox-forecast=0.15.3` as `h3`, and `=0.15.10` as `h10`) across AutoETS, AutoARIMA, ETS(AAA/MAM/MNM/AAdA), Theta, OptimizedTheta, AutoTheta, SES, Holt, Naive, Drift, SeasonalNaive, MSTL, Croston (Classic/Opt/SBA), TSB, ADIDA, IMAPA on AirPassengers, a random-walk series, and an intermittent-demand series. `diff h3/out_0153.txt h10/out_01510.txt` exits 0 — **byte-identical** output including all NATIVE and EXT (FFI-layer `forecast()`) point/lower/upper/resid_rms values. [VERIFIED: direct `diff`, this session, exit code 0]

### Anti-Patterns to Avoid
- **Writing a new `tests/` integration file for private-function fixtures:** `fit_ols_regression` is not `pub`; a new file under `crates/anofox-fcst-core/tests/` cannot see it. Either make it `pub(crate)` (a tiny, defensible visibility change — worth a one-line note in the plan) or keep the fixture-driven test inline in `forecast.rs`'s existing `#[cfg(test)]` block.
- **Trusting `cargo info`'s "rust-version: unknown" as evidence of no MSRV constraint:** `trueno` declares no `rust-version` field, but that is an absence, not a guarantee of compatibility with any given toolchain — the only way to know is to actually try to compile against it (done this session).
- **Assuming the `extension-ci-tools` WASM `@1.86.0` pin needs fixing:** it's a vendored submodule (`duckdb/extension-ci-tools`, fetched by GitHub Actions directly from the tag on GitHub, not from the local checkout) — editing the local copy would not even change CI behavior, and in any case `trueno`'s AVX-512 code is `x86_64`-gated so the WASM job never hits the MSRV-sensitive code path.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|--------------|-----|
| Multi-scale OLS reference values | A fresh synthetic-data generator + manual lstsq re-derivation | Port `upstream_fdars_reg/gen_reg.py` + `reg_main.rs` verbatim (already validated against both 0.5.3 and 0.5.15) | Re-deriving risks a transcription error in the exact betas/noise seed that reproduced the audit's scrambled-coefficient finding |
| MSRV bisection tooling | `cargo install cargo-msrv` (network + build time for a one-shot check) | Manual `cargo +<toolchain> check` against already-installed/`rustup toolchain install`-able toolchains | Confirmed in two commands per toolchain; `cargo-msrv` adds a dependency for a problem this phase only needs to solve once |
| Point-forecast parity proof | A new snapshot test from scratch across all ~30 `ModelType`s | Port `upstream_af/h3/src/main.rs` (parameterized by `DATA` env var, already proven byte-identical across versions) | The harness already enumerates every model surface (`NATIVE` crate-level + `EXT` FFI-level) the FFI dispatches through |

**Key insight:** this phase's highest-leverage move is archival, not invention — the prior audit session already built, ran, and validated the exact three reproducers (`gen_reg.py`/`reg_main.rs`, `h3`/`h10` snapshot harness, and upstream's own `issue_219_mfles_multiplicative_runaway.rs` + `global_model_nan_guards.rs`) that this phase's success criteria describe word-for-word. The research task was locating and verifying them, not designing new ones.

## Common Pitfalls

### Pitfall 1: Treating "no `rust-version` field" as "no MSRV problem"
**What goes wrong:** `cargo info trueno` reports `rust-version: unknown`; it is easy to read this as "any stable toolchain works."
**Why it happens:** absence of a declared floor is not evidence of universal compatibility — it is silence.
**How to avoid:** positively compile against the oldest claimed-supported toolchain. This session: `cargo +1.86.0 check -p anofox-fcst-core` → 450 errors, `error[E0658]: the target feature 'avx512f' is currently unstable` inside `trueno-0.8.9/src/backends/avx512.rs`. `cargo +1.88.0 check` → same error. `cargo +1.89.0 check` → succeeds. This holds for both the pre-bump tree (anofox-forecast=0.15.3) and the Phase-9-bumped tree (anofox-forecast=0.15.10, anofox-regression=0.5.15) — verified independently against both.
**Warning signs:** any crate with SIMD/target-feature code (`trueno` is pulled in transitively, not a direct dependency — `cargo tree -i trueno` is how it was found).

### Pitfall 2: Assuming the vendored CI submodule's hardcoded toolchain pin is a blocker
**What goes wrong:** `extension-ci-tools/.github/workflows/_extension_distribution.yml:1131` hardcodes `dtolnay/rust-toolchain@1.86.0` for the WASM job — at first glance this looks like it will break the moment the dependency tree needs 1.89.
**Why it happens:** the pin is real, but the code path it would break never executes: `trueno`'s AVX-512 backend module is declared `#[cfg(target_arch = "x86_64")]` [VERIFIED: `grep -n 'cfg(target_arch' trueno-0.8.9/src/backends/mod.rs:38`], so it is excluded from the `wasm32-unknown-emscripten` build entirely.
**How to avoid:** don't fix what isn't broken — confirmed this session with `cargo +1.86.0 check --target wasm32-unknown-emscripten` against the bumped-version throwaway project: compiles clean in ~65s, trueno included, no AVX-512 errors.
**Warning signs:** would only matter if a future `trueno` release moved AVX-512-adjacent code outside the `x86_64` cfg gate, or if `anofox-forecast` started requiring a newer `trueno` minor that changes this gating — re-verify the WASM `cargo check` whenever either dependency is bumped again.

### Pitfall 3: The duplicate `anofox-regression 0.4.0` in `Cargo.lock` is NOT this phase's problem
**What goes wrong:** after bumping `anofox-regression` to `=0.5.15`, `cargo tree -i anofox-regression` still shows **two** resolved versions (`0.4.0` and `0.5.15`) — it looks like the bump didn't fully take.
**Why it happens:** the duplicate comes from `fdars-core 0.3`'s own `linalg` feature, which depends on `anofox-regression ^0.4` for an unused functional-ridge helper — completely independent of the workspace's own `anofox-regression` dependency edge. [VERIFIED: reproduced this session with a throwaway project combining `anofox-forecast=0.15.10` + `anofox-regression=0.5.15` + `fdars-core 0.3 features=["parallel","linalg"]` — `cargo tree -i anofox-regression` reports "ambiguous specification" between `0.4.0` and `0.5.15`]
**How to avoid:** this is explicitly deferred to Phase 15 (DEP-03: fdars-core bump to 0.47, dropping the `linalg` feature). Do not attempt to drop `linalg` or otherwise touch `fdars-core`'s feature set in this phase — it would be premature scope creep into a different requirement's territory, and `WRAP-01`'s fdars call-site fixes (Phase 15) are what actually need the newer `fdars-core` API.
**Warning signs:** a plan task that tries to "clean up" the duplicate in Phase 9 is over-scoped; flag it back to Phase 15.

## Code Examples

### MSRV bisection (exact commands run this session, exact output)
```bash
# Fails — avx512f target feature unstable before Rust 1.89
cargo +1.88.0 check -p anofox-fcst-core
# error[E0658]: use of unstable library feature `stdarch_x86_avx512`
#   --> .../trueno-0.8.9/src/backends/avx512.rs:1461:13
# error: could not compile `trueno` (lib) due to 450 previous errors

# Succeeds
cargo +1.89.0 check -p anofox-fcst-core
#    Finished `dev` profile [unoptimized + debuginfo] target(s) in 1m 22s
```
Reproduced identically against a throwaway project pinned to the Phase-9 bumped versions (`anofox-forecast=0.15.10`, `anofox-regression=0.5.15`).

### Point-forecast snapshot diff (exact commands, exact result)
```bash
diff upstream_af/h3/out_0153.txt upstream_af/h10/out_01510.txt
echo "exit: $?"   # exit: 0  — byte-identical
```

### Multi-scale OLS fixture (ported from the audit scratchpad, numbers verified this session)
```python
# validation/reference/python/gen_multiscale_ols.py (port of gen_reg.py)
import numpy as np, json
rng = np.random.default_rng(1)
# price ~N(10,1), promo in {0,1}, temperature ~N(20,8), holiday flag P=0.03
X = np.column_stack([
    rng.normal(10, 1, 365),
    rng.integers(0, 2, 365),
    rng.normal(20, 8, 365),
    (rng.random(365) < 0.03).astype(float),
])
beta = np.array([-2., 5., 0.3, 10.])
y = X @ beta + 3.0 + rng.normal(0, 0.5, X.shape[0])
A = np.column_stack([np.ones(len(y)), X])
coef, *_ = np.linalg.lstsq(A, y, rcond=None)
# coef == [2.850942..., -1.985691..., 5.074206..., 0.297922..., 9.924729...]
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `anofox-regression =0.5.3` OLS fit on multi-scale regressors | `anofox-regression =0.5.15` | fixed in 0.5.13 (#26) + 0.5.14 (memory) | Coefficient order/sign no longer scrambled; 15k-row exog fits stop needing ~1.7GB |
| `anofox-forecast 0.15.3` MFLES auto-mode | `anofox-forecast 0.15.10` | fixed in 0.15.10 (#219) | Near-zero-value series no longer forecast at ~13× level in multiplicative mode |
| "Rust 1.86+" claim in `.claude/CLAUDE.md`/STACK.md | `rust-version = "1.89"` (this phase) | trueno's AVX-512 stabilization landed in rustc 1.89.0 | Documented floor now matches the actual compile requirement; `cargo` itself will now refuse to build on < 1.89 with a clear error instead of a confusing `trueno` compile failure |

**Deprecated/outdated:** the "Rust 1.86+ (stable, due to argmin patch)" framing in `.claude/CLAUDE.md:78` and `.planning/codebase/STACK.md:82` — the argmin patch is still needed (for `is_multiple_of`, stable since 1.87) but is no longer the binding constraint; `trueno`'s AVX-512 code is the actual floor at 1.89.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Per-family tolerance values proposed in the README template (e.g. `abs=1e-9` for moments, a looser relative tolerance for R-forecast point values) are Claude's discretion, not independently verified against a specific precision requirement | Standard Stack / Pattern 1 | Low — CONTEXT.md explicitly leaves tolerance choice to Claude's discretion; the planner/executor should pick values that make each specific fixture's comparison meaningful (e.g. R prints to 4 decimal places in `ref.R`, so tolerance should not demand more precision than the reference itself provides) |
| A2 | `fit_ols_regression` should become `pub(crate)` (rather than staying private and tested inline) is a recommendation, not a locked decision | Architecture Patterns / Anti-Patterns | Low — either approach satisfies TEST-01's "Rust test loads a fixture" requirement; the inline-`#[cfg(test)]` approach requires zero visibility changes and matches the established in-repo convention more closely |

**If this table is empty:** N/A — two low-risk discretionary items logged above; no claim central to DEP-01/DEP-02/DEP-04's correctness rests on unverified assumption.

## Open Questions

1. **Should the fixture JSON schema be uniform across families, or per-family?**
   - What we know: CONTEXT.md specifies the fixture file should contain "inputs, parameters, expected outputs, tool + package versions, tolerance" — a shape, not a strict schema.
   - What's unclear: whether later phases (STAT-02 feasts/STL, FEAT-05 tsfresh-117) will need a materially different shape that would make an early rigid schema a liability.
   - Recommendation: keep the four JSON examples in this document as the template, but don't over-formalize with a JSON Schema file in Phase 9 — let the shape stabilize organically across the first 2-3 families seeded here plus Phase 11's diagnostics family.

2. **Does `fit_ols_regression` need a visibility change, or does the fixture test live inline?**
   - What we know: the function is private; integration tests under `crates/anofox-fcst-core/tests/` cannot reach it; inline `#[cfg(test)]` blocks can.
   - What's unclear: whether the planner prefers the "small `pub(crate)` + real integration test" shape (more consistent with the new `tests/` directory TEST-01 establishes) over the "keep it private, test inline" shape (zero API surface change, but inconsistent with putting fixture tests in the new `tests/` dir).
   - Recommendation: default to inline (zero visibility change, matches existing codebase convention for `forecast.rs` specifically), but flag this as a 30-second decision point for whoever writes the plan task.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Rust toolchain 1.89.0+ | Compiling the bumped dependency tree | ✓ (installed this session via `rustup toolchain install`) | 1.89.0 confirmed sufficient; system default is 1.99.0 | — |
| Rust toolchain 1.86.0 (for falsification only) | Proving the MSRV claim | ✓ (pre-installed) | 1.86.0 | — |
| `rustup` | Toolchain bisection | ✓ | — | — |
| `cargo-msrv` | Automated MSRV bisection | ✗ (not installed) | — | Manual `cargo +<ver> check` bisection (used this session, no fallback needed) |
| R + forecast/tseries/urca/jsonlite | R-side fixture generators | ✓ | R 4.6.1 [VERIFIED: `Rscript --version`] | — |
| R feasts | Future STL-strength fixtures (not this phase) | ✗ | — | `stats::stl` (locked decision already accounts for this) |
| `uv` | Python fixture-env management | ✓ | 0.12.16 | — |
| Python (via uv) | numpy/scipy/pandas/statsmodels/tsfresh | ✓ (uv-managed CPython 3.12.12; all 5 packages install) | see Standard Stack table | — |
| System Python 3.14.7 | — | ✓ but lacks statsmodels/tsfresh | 3.14.7 | Not usable directly for fixture generation — uv project is required, matching the locked decision |
| DuckDB build toolchain (CMake, C++17 compiler) | Full extension build (not strictly needed to prove MSRV/dep-bump claims) | not probed this session | — | `cargo check`/`cargo test` at the Rust-core level is sufficient evidence for DEP-01/DEP-02/DEP-04; a full `make release` build should still be run once during execution as final confirmation per the phase's own success criterion #4 |

**Missing dependencies with no fallback:** none — `cargo-msrv`'s absence has a complete, already-exercised fallback (manual bisection).

**Missing dependencies with fallback:** R `feasts` (not needed this phase; `stats::stl` fallback already locked in by CONTEXT.md for the phase that needs it).

## Security Domain

This phase has no new SQL surface, no user input handling, and no authentication/session code — it is a dependency-version bump plus an out-of-band fixture harness. ASVS categories V2-V4 (auth/session/access-control) do not apply. The one security-adjacent concern is supply-chain risk from the two version bumps and the Package Legitimacy Audit above covers it: both `anofox-forecast` and `anofox-regression` are pre-existing, already-vetted first-party crates (same author/org as this extension) being version-bumped, not newly introduced dependencies — their `SUS` (low-downloads) verdict is an expected false positive for a narrow-purpose first-party crate, not a supply-chain red flag.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | no | N/A — no SQL/API surface touched |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A |
| V5 Input Validation | no (new code) | N/A — fixture loader reads only committed, version-controlled JSON, not external/untrusted input |
| V6 Cryptography | no | N/A |
| V14 Configuration (supply chain, adjacent) | yes | Exact version pins (`=0.5.15`, not a semver range) for both bumped crates, matching the existing `=0.5.3` exact-pin convention already in `Cargo.toml`; `Cargo.lock` is committed so the resolved dependency graph (including the known Phase-15-deferred `anofox-regression 0.4.0` duplicate) is reproducible |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Dependency confusion / slopsquatting on a version bump | Tampering | Exact version pins (`=X.Y.Z`); verified both target versions exist on crates.io and resolve to the expected source repos before editing `Cargo.toml` |
| Build-script supply-chain injection | Tampering | Neither `anofox-forecast` nor `anofox-regression` declares a network-calling `build.rs`; both have been compiled into every prior release of this extension without incident |

## Sources

### Primary (HIGH confidence — verified by direct execution this session)
- `cargo +1.86.0/+1.88.0/+1.89.0/+1.91.0 check` against both the as-is and the Phase-9-bumped dependency tree — MSRV floor
- `cargo +1.86.0 check --target wasm32-unknown-emscripten` against the bumped tree — WASM non-blocker confirmation
- `cargo tree -i trueno`, `cargo tree -i anofox-regression` — dependency-graph structure, duplicate `anofox-regression 0.4.0` confirmation
- `grep`/`Read` of `trueno-0.8.9/src/backends/mod.rs:22-38` — AVX-512 `cfg(target_arch = "x86_64")` gate
- `grep`/`Read` of `extension-ci-tools/.github/workflows/_extension_distribution.yml:830,834,1131` and `git fetch origin v1.4-andium` — CI toolchain pin inventory across both CI-tools versions in use
- `Rscript -e 'installed.packages()'` — R package availability
- `uv add numpy scipy pandas statsmodels tsfresh` — Python fixture-env feasibility and exact versions
- `uv run python3 -c "scipy.stats.skew/kurtosis(...)"` — exact verified numbers for the Pattern 1 example
- `Read` of `/home/simonm/projects/rust/anofox-forecast/CHANGELOG.md:10-90` — DEP-02 fix inventory (#219, panel NaN guards, Laplace leaves)
- `Read` of `/home/simonm/projects/rust/anofox-forecast/tests/issue_219_mfles_multiplicative_runaway.rs` and `tests/global_model_nan_guards.rs` — exact upstream regression-test reproducers
- `Read`/`diff` of the audit scratchpad (`upstream_af/{h3,h10}/out_*.txt`, `upstream_fdars_reg/{gen_reg.py,reg_main.rs,cmp_reg.py,reg_ref.json,out_reg053.json,out_reg0515.json}`) — DEP-01/DEP-02 before/after evidence
- `Read` of `crates/anofox-fcst-core/src/forecast.rs:3188-3230` — exact private function to fixture-test for DEP-01
- `Read` of `Cargo.toml`, `crates/anofox-fcst-core/Cargo.toml`, `crates/anofox-fcst-ffi/Cargo.toml`, `Cargo.lock` — current pins and existing test-support dependencies

### Secondary (MEDIUM confidence)
- `gsd_run query package-legitimacy check --ecosystem crates` — automated legitimacy signals (downloads/age/repo), cross-checked against first-party-crate reasoning above

### Tertiary (LOW confidence, corroborating only — not load-bearing)
- WebSearch "rust target_feature avx512f stabilized" (Phoronix article) — corroborates the 1.89.0 stabilization date; the actual claim in this document rests on the positive-falsification `cargo check` evidence, not this search

## Metadata

**Confidence breakdown:**
- Standard stack (dependency versions): HIGH — both versions confirmed to exist in the local registry cache and to compile
- Architecture (fixture harness layout): HIGH — directly derived from CONTEXT.md's locked decisions plus verified tooling availability
- MSRV (DEP-04): HIGH — positively falsified and positively confirmed against both pre- and post-bump dependency trees, plus the WASM non-blocker claim independently verified
- Pitfalls: HIGH — all three drawn from direct verification this session (not speculation)

**Research date:** 2026-10-08
**Valid until:** 30 days (dependency-version and MSRV facts are stable once pinned; re-verify if `anofox-forecast`/`anofox-regression`/`trueno` receive further releases before the plan executes)
