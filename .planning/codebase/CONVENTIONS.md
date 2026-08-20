# Coding Conventions

**Analysis Date:** 2026-08-20

## Naming Patterns

**Files:**
- Rust source files use snake_case: `decomposition.rs`, `detrending.rs`, `imputation.rs`
- Test modules are inline within source files using `#[cfg(test)] mod tests { }`
- Benchmark files use snake_case in `benches/` directory: `mstl_perf.rs`
- SQL test files use lowercase with underscores: `ts_diff.test`, `ts_features.test`

**Functions:**
- Public functions use snake_case: `mstl_decompose()`, `extract_features()`, `detect_changepoints()`
- Utility functions follow verb_noun pattern: `is_constant()`, `drop_edge_zeros()`, `fill_gaps()`
- Methods that validate/detect use verb prefixes: `detect_*`, `classify_*`, `compute_*`, `extract_*`, `analyze_*`
- Builder/conversion methods use `from_*` or `to_*` patterns

**Variables:**
- Local variables and parameters use snake_case: `non_null_count`, `seasonal_period`, `hazard_rate`
- Constants and type parameters use UPPER_SNAKE_CASE: `DEFAULT_TOLERANCE`, `HORIZON`, `SEASONAL_PERIOD`
- Generic type parameters use uppercase single letters: `T`, `F` (for function types)

**Types:**
- Structs use PascalCase: `MstlDecomposition`, `ForecastError`, `ConformalResult`
- Enums use PascalCase with variants as PascalCase: `PeriodMethod::Fft`, `InsufficientDataMode::Trend`
- Type aliases use PascalCase: `Result<T>` is defined as `type Result<T> = std::result::Result<T, ForecastError>`
- Result wrappers follow convention: `pub type Result<T> = std::result::Result<T, ForecastError>`

## Code Style

**Formatting:**
- Use standard Rust formatting (implied rustfmt defaults — no rustfmt.toml found)
- 4-space indentation (Rust default)
- Line length: standard (no specific limit enforced)
- Opening braces on same line: `fn foo() {` (Rust convention)

**Linting:**
- Standard Clippy lints apply (no custom configuration)
- Code follows idiomatic Rust patterns

## Import Organization

**Order:**
1. Internal crate imports (relative paths): `use crate::error::{ForecastError, Result};`
2. Standard library: `use std::str::FromStr;`
3. External dependencies: `use thiserror::Error;`, `use fdars_core::seasonal::{...};`
4. Re-exports in module root: `pub use bootstrap::{...};`

**Path Aliases:**
- No path aliases observed in crates (standard module system used)
- Crate-relative paths use `crate::` prefix explicitly

## Error Handling

**Patterns:**
- All fallible operations return `Result<T>` which is `std::result::Result<T, ForecastError>`
- Custom error types defined with `#[derive(Error, Debug)]` using `thiserror` crate
- Error variants include contextual information: `InvalidParameter { param, value, reason }`
- Errors map to numeric codes for FFI: `to_code()` method on `ForecastError`
- Example from `crates/anofox-fcst-core/src/error.rs`:
  ```rust
  #[derive(Error, Debug)]
  pub enum ForecastError {
      #[error("Null pointer argument: {0}")]
      NullPointer(String),
      #[error("Invalid parameter '{param}' = '{value}': {reason}")]
      InvalidParameter {
          param: String,
          value: String,
          reason: String,
      },
  }
  ```

## Logging

**Framework:** No logging framework observed; extension uses silent failures with error returns

**Patterns:**
- Errors propagated via `Result<T>` type, not logged
- FFI layer converts errors to numeric codes for caller interpretation
- No `println!` or `eprintln!` in library code (only in benchmarks)

## Comments

**When to Comment:**
- Module-level documentation with `//!` explaining purpose and usage
- Complex algorithms documented with multi-line comments
- Examples included in doc comments with code blocks
- Individual functions have doc comments with purpose, arguments, returns sections

**JSDoc/TSDoc:**
- Rust uses `///` for doc comments on public items
- Format: Summary line, then Arguments section, Returns section, Example section if applicable
- Example from `crates/anofox-fcst-core/src/filter.rs`:
  ```rust
  /// Checks if a series is constant (all non-NULL values are the same).
  ///
  /// A series is considered constant if all its non-NULL values are equal
  /// within floating-point epsilon tolerance.
  ///
  /// # Arguments
  /// * `values` - Slice of optional values to check
  ///
  /// # Returns
  /// `true` if the series is constant or has fewer than 2 non-NULL values
  ///
  /// # Example
  /// ```
  /// use anofox_fcst_core::filter::is_constant;
  /// assert!(is_constant(&[Some(5.0), Some(5.0), None, Some(5.0)]));
  /// ```
  ```

## Function Design

**Size:**
- Functions kept to 30-50 lines for core algorithms; helpers smaller (5-20 lines)
- Long functions (100+ lines) contain clear section comments for major steps

**Parameters:**
- Slices preferred over references to vectors: `fn foo(&[f64])`
- Optional values use `Option<f64>` and `Option<T>` patterns
- Return complex results via struct: `struct DetrendResult { detrended: Vec<f64>, ... }`
- No builder patterns observed; configuration via direct struct construction or default traits

**Return Values:**
- Simple values returned directly
- Multiple values wrapped in structs with named fields
- Errors wrapped in `Result<T>`
- Option used for nullable returns (e.g., trend component might be `Option<Vec<f64>>`)

## Module Design

**Exports:**
- Public types and functions explicitly declared as `pub`
- Re-exports in `lib.rs` to stabilize API: `pub use bootstrap::{...};`
- Private helpers marked implicitly without `pub` keyword

**Barrel Files:**
- Single `lib.rs` in `crates/anofox-fcst-core/src/lib.rs` re-exports all public items
- Allows users to import from crate root: `use anofox_fcst_core::mstl_decompose;`
- Pattern keeps internal module structure hidden while exposing clean API

---

*Convention analysis: 2026-08-20*
