//! Fixture loader shared by core-crate integration tests.
//!
//! std + serde_json ONLY — no core-crate or FFI-crate references. This file
//! is also compiled into the library's `cfg(test)` build via `#[path]`
//! starting in 09-03; any dependency on those crate paths would not resolve
//! there.
#![allow(dead_code)]

use serde_json::Value;
use std::path::{Path, PathBuf};

/// Absolute path to the repo root, derived from the crate's manifest dir
/// (`crates/anofox-fcst-core/..` -> workspace root `..`).
pub fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .expect("repo_root(): failed to canonicalize CARGO_MANIFEST_DIR/../..")
}

/// Absolute path to `validation/reference/fixtures`.
pub fn fixtures_dir() -> PathBuf {
    repo_root().join("validation/reference/fixtures")
}

/// Load and parse a fixture JSON file by path relative to `fixtures_dir()`.
pub fn load_fixture(rel: &str) -> Value {
    let path = fixtures_dir().join(rel);
    let text = std::fs::read_to_string(&path).unwrap_or_else(|e| {
        panic!(
            "load_fixture({rel:?}): failed to read {}: {e} (regenerate per validation/reference/README.md)",
            path.display()
        )
    });
    serde_json::from_str(&text).unwrap_or_else(|e| {
        panic!(
            "load_fixture({rel:?}): failed to parse {}: {e} (regenerate per validation/reference/README.md)",
            path.display()
        )
    })
}

/// Coerce a JSON value to f64: numbers pass through; null -> NaN; the
/// strings jsonlite uses for non-finite numerics ("NaN"/"NA", "Inf"/
/// "Infinity", "-Inf"/"-Infinity") map to the corresponding f64 special
/// value. Anything else panics naming the offending value.
pub fn num(v: &Value) -> f64 {
    match v {
        Value::Null => f64::NAN,
        Value::Number(n) => n
            .as_f64()
            .unwrap_or_else(|| panic!("num(): JSON number {n:?} has no f64 representation")),
        Value::String(s) => match s.as_str() {
            "NaN" | "NA" => f64::NAN,
            "Inf" | "Infinity" => f64::INFINITY,
            "-Inf" | "-Infinity" => f64::NEG_INFINITY,
            other => panic!("num(): unrecognized string value {other:?}"),
        },
        other => panic!("num(): unsupported JSON value {other:?}"),
    }
}

/// Map `num` over a JSON array; panics if `v` is not an array.
pub fn f64_vec(v: &Value) -> Vec<f64> {
    v.as_array()
        .unwrap_or_else(|| panic!("f64_vec(): expected a JSON array, got {v:?}"))
        .iter()
        .map(num)
        .collect()
}

/// Read `fixture["tolerance"][key]`; panics if absent. Tests must take
/// tolerances from the fixture, never hard-code them.
pub fn tolerance(fx: &Value, key: &str) -> f64 {
    fx.get("tolerance")
        .and_then(|t| t.get(key))
        .map(num)
        .unwrap_or_else(|| panic!("tolerance(): fixture has no tolerance.{key}"))
}

/// Assert two f64s are close within `abs_tol + rel_tol * |want|`. Passes
/// iff both NaN, or equal infinities (same sign), or within tolerance.
pub fn assert_close(got: f64, want: f64, abs_tol: f64, rel_tol: f64, ctx: &str) {
    if got.is_nan() && want.is_nan() {
        return;
    }
    if got.is_infinite() && want.is_infinite() && got == want {
        return;
    }
    let diff = (got - want).abs();
    let bound = abs_tol + rel_tol * want.abs();
    assert!(
        diff <= bound,
        "{ctx}: got {got}, want {want}, diff {diff} exceeds tolerance {bound}"
    );
}

/// Element-wise `assert_close` over two slices; asserts equal lengths first.
pub fn assert_vec_close(got: &[f64], want: &[f64], abs_tol: f64, rel_tol: f64, ctx: &str) {
    assert_eq!(
        got.len(),
        want.len(),
        "{ctx}: length mismatch, got {} want {}",
        got.len(),
        want.len()
    );
    for (i, (g, w)) in got.iter().zip(want.iter()).enumerate() {
        assert_close(*g, *w, abs_tol, rel_tol, &format!("{ctx}[{i}]"));
    }
}

/// Recursive, sorted, regular-files-only walk of `fixtures_dir()`.
pub fn all_fixture_files() -> Vec<PathBuf> {
    fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
        let entries = match std::fs::read_dir(dir) {
            Ok(e) => e,
            Err(_) => return,
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                walk(&path, out);
            } else if path.is_file() {
                out.push(path);
            }
        }
    }
    let mut out = Vec::new();
    walk(&fixtures_dir(), &mut out);
    out.sort();
    out
}
