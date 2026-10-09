//! Point-forecast snapshot (DEP-02 proof point).
//!
//! Captures `anofox_fcst_core::forecast::forecast()`'s point forecasts for
//! every `list_models()` model (plus AutoEnsemble, ETS-spec variants and
//! Laplace variants) on three series (AirPassengers, a seeded random walk,
//! a seeded intermittent series) and MFLES/AutoMFLES on the upstream
//! issue-219 reproducer — **before** the anofox-regression/anofox-forecast
//! version bump landing in this same plan (09-03).
//!
//! Default mode (compare): loads the committed fixture, recomputes every
//! entry on the current dependency tree, and fails if any entry outside
//! `parameters.allowed_to_change` (MFLES / AutoMFLES / Laplace) differs.
//!
//! Bless mode (`ANOFOX_BLESS_FORECAST_SNAPSHOT=1`): (re)writes the fixture
//! from the current dependency tree's output. Only re-bless when a phase
//! deliberately changes point forecasts — record the reason in that
//! phase's SUMMARY (see `parameters.rebless_policy` in the fixture itself).

mod common;

use anofox_fcst_core::forecast::{forecast, ForecastOptions, LaplaceVariant, ModelType};
use anofox_fcst_core::list_models;
use common::{f64_vec, fixtures_dir, load_fixture, repo_root, tolerance};
use serde_json::{json, Map, Value};
use std::collections::BTreeSet;
use std::panic::AssertUnwindSafe;
use std::str::FromStr;

/// Mirrors the `forecast()` seasonal_period-vs-model guard at
/// `forecast.rs` (the explicit-seasonal-period error list near line 616).
/// These models reject a seasonal_period > 1 when auto_detect_seasonality
/// is false, so they always get seasonal_period 0 here.
const NON_SEASONAL_MODELS: &[ModelType] = &[
    ModelType::Naive,
    ModelType::SES,
    ModelType::SESOptimized,
    ModelType::Holt,
    ModelType::RandomWalkDrift,
    ModelType::ARIMA,
    ModelType::CrostonClassic,
    ModelType::CrostonOptimized,
    ModelType::CrostonSBA,
    ModelType::TSB,
    ModelType::ADIDA,
    ModelType::IMAPA,
];

struct SeriesSpec {
    name: &'static str,
    values: Vec<f64>,
    frequency: usize,
    source: &'static str,
}

struct EntrySpec {
    label: String,
    model: ModelType,
    ets_spec: Option<String>,
    laplace_variant: Option<LaplaceVariant>,
}

/// The 24-point upstream issue-219 reproducer, copied verbatim from
/// `anofox-forecast` tests/issue_219_mfles_multiplicative_runaway.rs
/// (`make_issue_219_series()`): level ~2000 with a near-zero outlier (1.0)
/// at index 11 that triggered the pre-0.15.10 MFLES multiplicative runaway.
fn issue_219_values() -> Vec<f64> {
    vec![
        2100.0, 1950.0, 2200.0, 1800.0, 2050.0, 2300.0, 1900.0, 2150.0, 1850.0, 2400.0, 2000.0,
        1.0, 2050.0, 1950.0, 2100.0, 1800.0, 2200.0, 1900.0, 2050.0, 2300.0, 1850.0, 2150.0,
        2000.0, 2100.0,
    ]
}

fn build_series() -> Vec<SeriesSpec> {
    let air_fx = load_fixture("r_forecast/air_passengers.json");
    let air_values = f64_vec(&air_fx["inputs"]["values"]);

    let sim_fx = load_fixture("r_forecast/simulated_series.json");
    let rw_values = f64_vec(&sim_fx["series"]["rw"]["inputs"]["values"]);
    let inter_values = f64_vec(&sim_fx["series"]["inter"]["inputs"]["values"]);

    vec![
        SeriesSpec {
            name: "air",
            values: air_values,
            frequency: 12,
            source: "validation/reference/fixtures/r_forecast/air_passengers.json#inputs.values",
        },
        SeriesSpec {
            name: "rw",
            values: rw_values,
            frequency: 1,
            source: "validation/reference/fixtures/r_forecast/simulated_series.json#series.rw.inputs.values",
        },
        SeriesSpec {
            name: "inter",
            values: inter_values,
            frequency: 1,
            source: "validation/reference/fixtures/r_forecast/simulated_series.json#series.inter.inputs.values",
        },
        SeriesSpec {
            name: "issue_219",
            values: issue_219_values(),
            frequency: 12,
            source: "anofox-forecast (upstream crate) tests/issue_219_mfles_multiplicative_runaway.rs::make_issue_219_series() — copied verbatim",
        },
    ]
}

/// Builds the 41-label set used for air/rw/inter: every `list_models()`
/// name (plain ETS = default spec AAA, plain Laplace = Auto variant) plus
/// "AutoEnsemble", plus 3 ETS spec variants and 2 Laplace variants.
fn standard_entry_specs(frequency: usize) -> Vec<EntrySpec> {
    let mut specs: Vec<EntrySpec> = list_models()
        .into_iter()
        .map(|name| {
            let model = ModelType::from_str(&name).unwrap_or_else(|e| {
                panic!("standard_entry_specs: list_models() name {name:?} does not parse: {e:?}")
            });
            EntrySpec {
                label: name,
                model,
                ets_spec: None,
                laplace_variant: None,
            }
        })
        .collect();

    specs.push(EntrySpec {
        label: "AutoEnsemble".to_string(),
        model: ModelType::AutoEnsemble,
        ets_spec: None,
        laplace_variant: None,
    });

    let ets_specs: &[&str] = if frequency > 1 {
        &["MAM", "MNM", "AAdA"]
    } else {
        &["ANN", "AAN", "AAdN"]
    };
    for spec in ets_specs {
        specs.push(EntrySpec {
            label: format!("ETS[{spec}]"),
            model: ModelType::ETS,
            ets_spec: Some(spec.to_string()),
            laplace_variant: None,
        });
    }

    for (tag, variant) in [
        ("auto_aid", LaplaceVariant::AutoAid),
        ("skaters", LaplaceVariant::Skaters),
    ] {
        specs.push(EntrySpec {
            label: format!("Laplace[{tag}]"),
            model: ModelType::Laplace,
            ets_spec: None,
            laplace_variant: Some(variant),
        });
    }

    specs
}

fn issue_219_entry_specs() -> Vec<EntrySpec> {
    vec![
        EntrySpec {
            label: "MFLES".to_string(),
            model: ModelType::MFLES,
            ets_spec: None,
            laplace_variant: None,
        },
        EntrySpec {
            label: "AutoMFLES".to_string(),
            model: ModelType::AutoMFLES,
            ets_spec: None,
            laplace_variant: None,
        },
    ]
}

fn seasonal_period_for(model: ModelType, frequency: usize) -> usize {
    if NON_SEASONAL_MODELS.contains(&model) {
        0
    } else if frequency > 1 {
        frequency
    } else {
        0
    }
}

fn encode_f64(v: f64) -> Value {
    if v.is_nan() {
        Value::String("NaN".to_string())
    } else if v == f64::INFINITY {
        Value::String("Infinity".to_string())
    } else if v == f64::NEG_INFINITY {
        Value::String("-Infinity".to_string())
    } else {
        json!(v)
    }
}

fn panic_message(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        s.to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "panic: non-string payload".to_string()
    }
}

/// Runs one (series, label) entry inside `catch_unwind` and records its
/// outcome class ("ok" | "error" | "panic").
fn run_entry(values: &[f64], frequency: usize, spec: &EntrySpec) -> Value {
    let seasonal_period = seasonal_period_for(spec.model, frequency);
    let options = ForecastOptions {
        model: spec.model,
        ets_spec: spec.ets_spec.clone(),
        horizon: 12,
        seasonal_period,
        auto_detect_seasonality: false,
        laplace_variant: spec.laplace_variant,
        ..Default::default()
    };

    let input: Vec<Option<f64>> = values.iter().copied().map(Some).collect();

    let outcome = std::panic::catch_unwind(AssertUnwindSafe(|| forecast(&input, &options)));

    let mut obj = Map::new();
    obj.insert(
        "model".to_string(),
        Value::String(spec.model.name().to_string()),
    );
    obj.insert("seasonal_period".to_string(), json!(seasonal_period));

    match outcome {
        Ok(Ok(output)) => {
            obj.insert("outcome".to_string(), Value::String("ok".to_string()));
            obj.insert(
                "model_name".to_string(),
                Value::String(output.model_name.clone()),
            );
            let point: Vec<Value> = output.point.iter().map(|v| encode_f64(*v)).collect();
            obj.insert("point".to_string(), Value::Array(point));
        }
        Ok(Err(e)) => {
            obj.insert("outcome".to_string(), Value::String("error".to_string()));
            obj.insert("message".to_string(), Value::String(format!("{e}")));
        }
        Err(payload) => {
            obj.insert("outcome".to_string(), Value::String("panic".to_string()));
            obj.insert(
                "message".to_string(),
                Value::String(panic_message(&*payload)),
            );
        }
    }

    Value::Object(obj)
}

fn generate_entries_and_series() -> (Map<String, Value>, Map<String, Value>) {
    let mut entries = Map::new();
    let mut series_obj = Map::new();

    for series in build_series() {
        let specs = if series.name == "issue_219" {
            issue_219_entry_specs()
        } else {
            standard_entry_specs(series.frequency)
        };

        for spec in &specs {
            let key = format!("{}/{}", series.name, spec.label);
            let entry = run_entry(&series.values, series.frequency, spec);
            entries.insert(key, entry);
        }

        let mut series_entry = Map::new();
        series_entry.insert(
            "values".to_string(),
            Value::Array(series.values.iter().map(|v| encode_f64(*v)).collect()),
        );
        series_entry.insert("frequency".to_string(), json!(series.frequency));
        series_entry.insert(
            "source".to_string(),
            Value::String(series.source.to_string()),
        );
        series_obj.insert(series.name.to_string(), Value::Object(series_entry));
    }

    (entries, series_obj)
}

/// Returns every `version = "..."` line immediately following a
/// `name = "<crate_name>"` line in the workspace `Cargo.lock` — std-only,
/// no toml parser, matching the loader's std+serde_json-only convention.
fn cargo_lock_versions(crate_name: &str) -> Vec<String> {
    let path = repo_root().join("Cargo.lock");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cargo_lock_versions: cannot read {}: {e}", path.display()));
    let lines: Vec<&str> = text.lines().collect();
    let marker = format!("name = \"{crate_name}\"");
    let mut versions = Vec::new();
    for i in 0..lines.len() {
        if lines[i].trim() == marker {
            if let Some(next) = lines.get(i + 1) {
                if let Some(v) = next.trim().strip_prefix("version = \"") {
                    versions.push(v.trim_end_matches('"').to_string());
                }
            }
        }
    }
    versions
}

/// For `anofox-regression`, Cargo.lock carries two resolved versions once
/// `fdars-core`'s `linalg` feature pulls its own `^0.4` edge (09-RESEARCH
/// Pitfall 3, Phase 15 DEP-03's problem, not this phase's) — pick the
/// workspace's own 0.5.x entry, never the 0.4.x one.
fn cargo_lock_version(crate_name: &str) -> String {
    let versions = cargo_lock_versions(crate_name);
    if crate_name == "anofox-regression" {
        versions
            .into_iter()
            .find(|v| !v.starts_with("0.4."))
            .unwrap_or_else(|| {
                panic!("cargo_lock_version: no non-0.4.x anofox-regression entry found")
            })
    } else {
        versions.into_iter().next().unwrap_or_else(|| {
            panic!("cargo_lock_version: no Cargo.lock entry found for {crate_name}")
        })
    }
}

/// Describes how an ALLOWED-model entry changed, for the eprintln evidence
/// line — either an outcome-class transition or the max abs point diff.
fn allowed_diff_description(old: &Value, new: &Value) -> String {
    let old_outcome = old["outcome"].as_str().unwrap_or("?");
    let new_outcome = new["outcome"].as_str().unwrap_or("?");
    if old_outcome != new_outcome {
        return format!("outcome {old_outcome}->{new_outcome}");
    }
    if old_outcome == "ok" {
        let old_point = f64_vec(&old["point"]);
        let new_point = f64_vec(&new["point"]);
        if old_point.len() == new_point.len() {
            let max_abs = old_point
                .iter()
                .zip(new_point.iter())
                .map(|(a, b)| (a - b).abs())
                .fold(0.0_f64, f64::max);
            return format!("max_abs_diff={max_abs}");
        }
        return format!("point length {}->{}", old_point.len(), new_point.len());
    }
    "same outcome class".to_string()
}

/// Strict (non-allowed) comparison: equal outcome, equal model_name for
/// "ok" entries, every point within `point_abs + point_rel * |want|`.
fn strict_compare(
    key: &str,
    fixture_entry: &Value,
    recomputed_entry: &Value,
    point_abs: f64,
    point_rel: f64,
    violations: &mut Vec<String>,
) {
    let want_outcome = fixture_entry["outcome"].as_str().unwrap_or("?");
    let got_outcome = recomputed_entry["outcome"].as_str().unwrap_or("?");
    if want_outcome != got_outcome {
        violations.push(format!(
            "{key}: outcome {want_outcome:?} -> {got_outcome:?} ({:?} -> {:?})",
            fixture_entry.get("message"),
            recomputed_entry.get("message")
        ));
        return;
    }

    if want_outcome != "ok" {
        return;
    }

    let want_name = fixture_entry["model_name"].as_str().unwrap_or("");
    let got_name = recomputed_entry["model_name"].as_str().unwrap_or("");
    if want_name != got_name {
        violations.push(format!("{key}: model_name {want_name:?} -> {got_name:?}"));
    }

    let want_point = f64_vec(&fixture_entry["point"]);
    let got_point = f64_vec(&recomputed_entry["point"]);
    if want_point.len() != got_point.len() {
        violations.push(format!(
            "{key}: point length {} -> {}",
            want_point.len(),
            got_point.len()
        ));
        return;
    }

    for (i, (w, g)) in want_point.iter().zip(got_point.iter()).enumerate() {
        if w.is_nan() && g.is_nan() {
            continue;
        }
        if w.is_infinite() && g.is_infinite() && w == g {
            continue;
        }
        let diff = (w - g).abs();
        let bound = point_abs + point_rel * w.abs();
        if diff > bound {
            violations.push(format!(
                "{key}: point[{i}] got {g} want {w} diff {diff} exceeds tolerance {bound}"
            ));
        }
    }
}

#[test]
fn point_forecasts_match_snapshot() {
    let start = std::time::Instant::now();
    let (entries, series_obj) = generate_entries_and_series();
    let elapsed = start.elapsed();

    let bless = std::env::var("ANOFOX_BLESS_FORECAST_SNAPSHOT")
        .map(|v| v == "1")
        .unwrap_or(false);

    if bless {
        let anofox_forecast_version = cargo_lock_version("anofox-forecast");
        let anofox_regression_version = cargo_lock_version("anofox-regression");

        let mut tool_versions = Map::new();
        tool_versions.insert(
            "anofox-forecast".to_string(),
            Value::String(anofox_forecast_version),
        );
        tool_versions.insert(
            "anofox-regression".to_string(),
            Value::String(anofox_regression_version),
        );
        tool_versions.insert(
            "anofox-fcst-core".to_string(),
            Value::String(env!("CARGO_PKG_VERSION").to_string()),
        );
        tool_versions.insert(
            "target".to_string(),
            Value::String(format!(
                "{}-{}",
                std::env::consts::ARCH,
                std::env::consts::OS
            )),
        );

        let mut tolerance_obj = Map::new();
        tolerance_obj.insert("point_abs".to_string(), json!(1e-6));
        tolerance_obj.insert("point_rel".to_string(), json!(1e-6));
        tolerance_obj.insert(
            "notes".to_string(),
            Value::String(
                "absorbs SIMD-backend summation-order differences between machines (trueno \
                 runtime dispatch), same-machine runs are bit-identical; if CI on other \
                 hardware exceeds it, widen point_rel here with the measured value and \
                 rationale, never per-test"
                    .to_string(),
            ),
        );

        let mut allowed_to_change = Map::new();
        allowed_to_change.insert(
            "MFLES".to_string(),
            Value::String(
                "anofox-forecast 0.15.10 #219: additive fallback for near-zero series, log \
                 winsorizing, back-transform clamp"
                    .to_string(),
            ),
        );
        allowed_to_change.insert(
            "AutoMFLES".to_string(),
            Value::String(
                "anofox-forecast 0.15.10 #219: additive fallback for near-zero series, log \
                 winsorizing, back-transform clamp"
                    .to_string(),
            ),
        );
        allowed_to_change.insert(
            "Laplace".to_string(),
            Value::String(
                "anofox-forecast 0.15.7 Laplace leaf fixes and 0.15.9 NaN/Inf guards".to_string(),
            ),
        );

        let mut parameters = Map::new();
        parameters.insert("horizon".to_string(), json!(12));
        parameters.insert(
            "allowed_to_change".to_string(),
            Value::Object(allowed_to_change),
        );
        parameters.insert(
            "rebless_policy".to_string(),
            Value::String(
                "re-bless only when a phase deliberately changes point forecasts; record the \
                 reason in that phase SUMMARY"
                    .to_string(),
            ),
        );

        let mut fixture = Map::new();
        fixture.insert(
            "family".to_string(),
            Value::String("forecast_snapshot".to_string()),
        );
        fixture.insert(
            "generator".to_string(),
            Value::String("crates/anofox-fcst-core/tests/forecast_snapshot.rs".to_string()),
        );
        fixture.insert("tool_versions".to_string(), Value::Object(tool_versions));
        fixture.insert("tolerance".to_string(), Value::Object(tolerance_obj));
        fixture.insert("parameters".to_string(), Value::Object(parameters));
        fixture.insert("series".to_string(), Value::Object(series_obj));
        fixture.insert("entries".to_string(), Value::Object(entries));

        let out_dir = fixtures_dir().join("forecast_snapshot");
        std::fs::create_dir_all(&out_dir)
            .unwrap_or_else(|e| panic!("cannot create {}: {e}", out_dir.display()));
        let out_path = out_dir.join("point_forecasts.json");
        let mut text = serde_json::to_string_pretty(&Value::Object(fixture))
            .expect("serialize forecast_snapshot fixture");
        text.push('\n');
        std::fs::write(&out_path, &text)
            .unwrap_or_else(|e| panic!("cannot write {}: {e}", out_path.display()));

        let reloaded = load_fixture("forecast_snapshot/point_forecasts.json");
        let reloaded_entries = reloaded["entries"]
            .as_object()
            .expect("reloaded fixture has an 'entries' object");
        assert_eq!(
            reloaded_entries.len(),
            125,
            "bless: expected 125 entries, got {}",
            reloaded_entries.len()
        );

        eprintln!(
            "forecast_snapshot: BLESS wrote {} entries in {elapsed:?}",
            reloaded_entries.len()
        );
        return;
    }

    // Compare mode (default).
    let fixture = load_fixture("forecast_snapshot/point_forecasts.json");
    let fixture_entries = fixture["entries"]
        .as_object()
        .expect("fixture has an 'entries' object");

    let fixture_keys: BTreeSet<&str> = fixture_entries.keys().map(|k| k.as_str()).collect();
    let recomputed_keys: BTreeSet<&str> = entries.keys().map(|k| k.as_str()).collect();
    if fixture_keys != recomputed_keys {
        let added: Vec<&str> = recomputed_keys.difference(&fixture_keys).copied().collect();
        let removed: Vec<&str> = fixture_keys.difference(&recomputed_keys).copied().collect();
        panic!(
            "point_forecasts_match_snapshot: entry key set changed (re-bless deliberately if \
             this is intended) added={added:?} removed={removed:?}"
        );
    }

    let fixture_forecast_version = fixture["tool_versions"]["anofox-forecast"]
        .as_str()
        .expect("fixture tool_versions.anofox-forecast")
        .to_string();
    let current_forecast_version = cargo_lock_version("anofox-forecast");
    let same_version = current_forecast_version == fixture_forecast_version;

    let allowed_to_change: BTreeSet<String> = fixture["parameters"]["allowed_to_change"]
        .as_object()
        .map(|m| m.keys().cloned().collect())
        .unwrap_or_default();

    let point_abs = tolerance(&fixture, "point_abs");
    let point_rel = tolerance(&fixture, "point_rel");

    let mut violations: Vec<String> = Vec::new();

    for key in fixture_keys.iter() {
        let fixture_entry = fixture_entries
            .get(*key)
            .unwrap_or_else(|| panic!("missing fixture entry for {key}"));
        let recomputed_entry = entries
            .get(*key)
            .unwrap_or_else(|| panic!("missing recomputed entry for {key}"));
        let entry_model = fixture_entry["model"].as_str().unwrap_or("");

        if !same_version && allowed_to_change.contains(entry_model) {
            let desc = allowed_diff_description(fixture_entry, recomputed_entry);
            if fixture_entry == recomputed_entry {
                eprintln!("ALLOWED-SAME {key}");
            } else {
                eprintln!("ALLOWED-CHANGE {key} {desc}");
            }
            continue;
        }

        strict_compare(
            key,
            fixture_entry,
            recomputed_entry,
            point_abs,
            point_rel,
            &mut violations,
        );
    }

    eprintln!(
        "point_forecasts_match_snapshot: compared {} entries in {elapsed:?} \
         (anofox-forecast current={current_forecast_version:?} fixture={fixture_forecast_version:?} same_version={same_version})",
        fixture_entries.len()
    );

    assert!(
        violations.is_empty(),
        "point_forecasts_match_snapshot: {} violation(s):\n{}",
        violations.len(),
        violations.join("\n")
    );
}
