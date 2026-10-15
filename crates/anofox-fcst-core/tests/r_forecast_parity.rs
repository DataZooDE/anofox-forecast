//! FCST-09 R `forecast` parity suite (Phase 17 plan 17-03).
//!
//! Drives `anofox_fcst_core::forecast::forecast` against every entry of
//! `tolerance.parity_checks` in both `r_forecast` fixtures. Every tolerance,
//! status, cause and reason is read from the fixture — nothing is
//! hard-coded here. D-12: a `known_gap` check's `cause` must cite an
//! existing `tolerance.root_causes` row located `crate` or `reference`.
//! D-15: every `AutoARIMA` check must be `status = "parity"` with its order
//! (parsed from `model_name`) matching R's `fit.order` exactly.

mod common;

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use serde_json::Value;
use std::str::FromStr;

const H: usize = 12;
const LEVEL: f64 = 0.95;

fn air_fixture() -> Value {
    common::load_fixture("r_forecast/air_passengers.json")
}
fn sim_fixture() -> Value {
    common::load_fixture("r_forecast/simulated_series.json")
}

/// One `tolerance.parity_checks` row plus the series values and R model
/// block it refers to, resolved from whichever fixture owns `series`.
struct Check {
    series: String,
    r_key: String,
    surface: String,
    ets_spec: String,
    seasonal_period: usize,
    point_tol_kind: String,
    point_tol: f64,
    interval_tol: Option<f64>,
    status: String,
    cause: String,
    reason: String,
    values: Vec<f64>,
    r_block: Value,
}

fn parse_checks(fixture: &Value, series_values: impl Fn(&str) -> Vec<f64>) -> Vec<Check> {
    let checks = fixture["tolerance"]["parity_checks"]
        .as_array()
        .cloned()
        .unwrap_or_default();
    checks
        .into_iter()
        .map(|c| {
            let series = c["series"].as_str().unwrap().to_string();
            let r_key = c["r_key"].as_str().unwrap().to_string();
            let surface = c["surface"].as_str().unwrap().to_string();
            let r_block = if series == "air" {
                fixture["models"][&r_key].clone()
            } else {
                fixture["series"][&series]["models"][&r_key].clone()
            };
            Check {
                series: series.clone(),
                r_key,
                surface,
                ets_spec: c["ets_spec"].as_str().unwrap_or("").to_string(),
                seasonal_period: c["seasonal_period"].as_u64().unwrap_or(0) as usize,
                point_tol_kind: c["point_tol_kind"].as_str().unwrap().to_string(),
                point_tol: common::num(&c["point_tol"]),
                interval_tol: {
                    let v = common::num(&c["interval_tol"]);
                    if v.is_finite() {
                        Some(v)
                    } else {
                        None
                    }
                },
                status: c["status"].as_str().unwrap().to_string(),
                cause: c["cause"].as_str().unwrap_or("").to_string(),
                reason: c["reason"].as_str().unwrap_or("").to_string(),
                values: series_values(&series),
                r_block,
            }
        })
        .collect()
}

fn air_values() -> Vec<f64> {
    common::f64_vec(&air_fixture()["inputs"]["values"])
}

fn sim_series_values(fixture: &Value, series: &str) -> Vec<f64> {
    common::f64_vec(&fixture["series"][series]["inputs"]["values"])
}

fn all_checks() -> Vec<Check> {
    let air_fx = air_fixture();
    let sim_fx = sim_fixture();
    let air_vals = air_values();
    let mut out = parse_checks(&air_fx, |_| air_vals.clone());
    out.extend(parse_checks(&sim_fx, |s| sim_series_values(&sim_fx, s)));
    out
}

fn max_rel_dev(got: &[f64], want: &[f64]) -> f64 {
    let mut worst = 0.0_f64;
    for (g, w) in got.iter().zip(want) {
        let dev = if w.abs() > 0.0 {
            (g - w).abs() / w.abs()
        } else {
            (g - w).abs()
        };
        worst = worst.max(dev);
    }
    worst
}

fn max_abs_dev(got: &[f64], want: &[f64]) -> f64 {
    got.iter()
        .zip(want)
        .map(|(g, w)| (g - w).abs())
        .fold(0.0_f64, f64::max)
}

fn max_interval_dev(lower: &[f64], upper: &[f64], r_lower: &[f64], r_upper: &[f64]) -> f64 {
    let mut worst = 0.0_f64;
    for i in 0..lower.len() {
        let r_half = (r_upper[i] - r_lower[i]).abs() / 2.0;
        let dlo = (lower[i] - r_lower[i]).abs();
        let dup = (upper[i] - r_upper[i]).abs();
        let dev = if r_half > 0.0 {
            dlo.max(dup) / r_half
        } else {
            dlo.max(dup)
        };
        worst = worst.max(dev);
    }
    worst
}

/// Parse "AutoARIMA(p,d,q)" or "AutoARIMA(p,d,q)(P,D,Q)[s]" from model_name.
#[allow(clippy::type_complexity)]
fn parse_autoarima_order(
    model_name: &str,
) -> Option<(usize, usize, usize, Option<(usize, usize, usize, usize)>)> {
    let rest = model_name.strip_prefix("AutoARIMA(")?;
    let close = rest.find(')')?;
    let non_seasonal = &rest[..close];
    let mut parts = non_seasonal.split(',');
    let p = parts.next()?.trim().parse().ok()?;
    let d = parts.next()?.trim().parse().ok()?;
    let q = parts.next()?.trim().parse().ok()?;
    let after = &rest[close + 1..];
    if let Some(seasonal_start) = after.find('(') {
        let seasonal_rest = &after[seasonal_start + 1..];
        let seasonal_close = seasonal_rest.find(')')?;
        let seasonal = &seasonal_rest[..seasonal_close];
        let mut sparts = seasonal.split(',');
        let sp = sparts.next()?.trim().parse().ok()?;
        let sd = sparts.next()?.trim().parse().ok()?;
        let sq = sparts.next()?.trim().parse().ok()?;
        let bracket_start = seasonal_rest.find('[')?;
        let bracket_rest = &seasonal_rest[bracket_start + 1..];
        let bracket_close = bracket_rest.find(']')?;
        let s: usize = bracket_rest[..bracket_close].trim().parse().ok()?;
        Some((p, d, q, Some((sp, sd, sq, s))))
    } else {
        Some((p, d, q, None))
    }
}

/// D-12: a known_gap check's cause must cite an existing root_causes row,
/// with at least one cited row located crate or reference.
fn cause_is_backed(cause: &str, root_causes: &[Value]) -> bool {
    let ids: Vec<&str> = cause
        .split(',')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .collect();
    if ids.is_empty() {
        return false;
    }
    let mut all_exist = true;
    let mut has_crate_or_ref = false;
    for id in &ids {
        match root_causes.iter().find(|r| r["id"].as_str() == Some(id)) {
            Some(row) => {
                if matches!(row["location"].as_str(), Some("crate") | Some("reference")) {
                    has_crate_or_ref = true;
                }
            }
            None => all_exist = false,
        }
    }
    all_exist && has_crate_or_ref
}

#[test]
fn r_forecast_parity_checks() {
    let air_fx = air_fixture();
    let root_causes: Vec<Value> = air_fx["tolerance"]["root_causes"]
        .as_array()
        .cloned()
        .unwrap_or_default();

    let checks = all_checks();
    assert!(
        !checks.is_empty(),
        "no tolerance.parity_checks rows found in either fixture"
    );

    let mut measured = Vec::new();

    for c in &checks {
        let model = ModelType::from_str(c.surface.split(':').next().unwrap())
            .unwrap_or_else(|_| panic!("unrecognised surface model name: {}", c.surface));
        let ets_spec = if c.ets_spec.is_empty() {
            None
        } else {
            Some(c.ets_spec.as_str())
        };

        let opts: Vec<Option<f64>> = c.values.iter().map(|v| Some(*v)).collect();
        let options = ForecastOptions {
            model,
            horizon: H,
            confidence_level: LEVEL,
            seasonal_period: c.seasonal_period,
            auto_detect_seasonality: false,
            ets_spec: ets_spec.map(|s| s.to_string()),
            ..Default::default()
        };
        let out = forecast(&opts, &options).unwrap_or_else(|e| {
            panic!(
                "forecast() failed for {}/{}/{}: {e}",
                c.series, c.r_key, c.surface
            )
        });

        let r_point = common::f64_vec(&c.r_block["point"]);
        let point_dev = match c.point_tol_kind.as_str() {
            "abs" => max_abs_dev(&out.point, &r_point),
            "rel" => max_rel_dev(&out.point, &r_point),
            other => panic!("unknown point_tol_kind {other:?}"),
        };

        let interval_dev =
            if let (Some(tol), true) = (c.interval_tol, !c.r_block["lower_95"].is_null()) {
                let r_lower = common::f64_vec(&c.r_block["lower_95"]);
                let r_upper = common::f64_vec(&c.r_block["upper_95"]);
                let dev = max_interval_dev(&out.lower, &out.upper, &r_lower, &r_upper);
                for i in 0..out.point.len() {
                    assert!(
                        out.lower[i] <= out.point[i] && out.point[i] <= out.upper[i],
                        "{}/{}/{} h={i}: lower<=point<=upper violated ({} <= {} <= {})",
                        c.series,
                        c.r_key,
                        c.surface,
                        out.lower[i],
                        out.point[i],
                        out.upper[i]
                    );
                }
                let _ = tol;
                Some(dev)
            } else {
                None
            };

        measured.push((
            c.series.clone(),
            c.r_key.clone(),
            c.surface.clone(),
            point_dev,
            interval_dev,
        ));

        assert!(
            point_dev <= c.point_tol,
            "{}/{}/{} point_dev={:.6e} exceeds point_tol={:.6e}",
            c.series,
            c.r_key,
            c.surface,
            point_dev,
            c.point_tol
        );
        if let (Some(dev), Some(tol)) = (interval_dev, c.interval_tol) {
            assert!(
                dev <= tol,
                "{}/{}/{} interval_dev={:.6e} exceeds interval_tol={:.6e}",
                c.series,
                c.r_key,
                c.surface,
                dev,
                tol
            );
        }

        if c.status == "known_gap" {
            assert!(
                !c.reason.is_empty(),
                "{}/{}/{} is known_gap with an empty reason",
                c.series,
                c.r_key,
                c.surface
            );
            assert!(
                cause_is_backed(&c.cause, &root_causes),
                "{}/{}/{} is known_gap with cause {:?} not backed by a crate/reference root_causes row",
                c.series,
                c.r_key,
                c.surface,
                c.cause
            );
        }
        if c.point_tol < 1e-6 || c.interval_tol.map(|t| t < 1e-6).unwrap_or(false) {
            assert!(
                !c.reason.is_empty(),
                "{}/{}/{} has a tolerance tighter than 1e-6 with an empty reason",
                c.series,
                c.r_key,
                c.surface
            );
        }

        // D-15: every AutoARIMA check is parity with a matching R order.
        if c.surface == "AutoARIMA" {
            assert_eq!(
                c.status, "parity",
                "{}/{}/AutoARIMA must be status parity (D-15)",
                c.series, c.r_key
            );
            let r_order = &c.r_block["fit"]["order"];
            let r_p = r_order["p"].as_u64().unwrap() as usize;
            let r_d = r_order["d"].as_u64().unwrap() as usize;
            let r_q = r_order["q"].as_u64().unwrap() as usize;
            let r_seasonal = r_order.get("P").map(|_| {
                (
                    r_order["P"].as_u64().unwrap() as usize,
                    r_order["D"].as_u64().unwrap() as usize,
                    r_order["Q"].as_u64().unwrap() as usize,
                    r_order["Frequency"].as_u64().unwrap() as usize,
                )
            });
            let parsed = parse_autoarima_order(&out.model_name);
            let order_match = match (&parsed, &r_seasonal) {
                (Some((p, d, q, Some((cp, cd, cq, s)))), Some((rp, rd, rq, rs))) => {
                    *p == r_p
                        && *d == r_d
                        && *q == r_q
                        && *cp == *rp
                        && *cd == *rd
                        && *cq == *rq
                        && *s == *rs
                }
                (Some((p, d, q, None)), None) => *p == r_p && *d == r_d && *q == r_q,
                _ => false,
            };
            assert!(
                order_match,
                "{}/auto_arima/AutoARIMA order mismatch: crate model_name={} vs R order p={r_p} d={r_d} q={r_q} seasonal={r_seasonal:?}",
                c.series, out.model_name
            );
        }

        // Parity-status AutoETS checks: selected spec must match R's method.
        // model_name is "AutoETS(<spec>)" vs R's "ETS(<spec>)" — compare the
        // parenthesised spec only, not the model-family prefix.
        if c.surface == "AutoETS" && c.status == "parity" {
            if let Some(r_method) = c.r_block["fit"]["method"].as_str() {
                let crate_spec = out.model_name.split_once('(').map(|x| x.1).unwrap_or("");
                let r_spec = r_method.split_once('(').map(|x| x.1).unwrap_or("");
                assert_eq!(
                    crate_spec, r_spec,
                    "{}/ets_auto/AutoETS selected spec must match R's method (crate={}, R={})",
                    c.series, out.model_name, r_method
                );
            }
        }
    }

    eprintln!("=== r_forecast_parity measured table ===");
    eprintln!("series/r_key/surface point_dev interval_dev");
    for (series, r_key, surface, point_dev, interval_dev) in &measured {
        eprintln!(
            "{series}/{r_key}/{surface} point={point_dev:.6e} interval={}",
            interval_dev
                .map(|d| format!("{d:.6e}"))
                .unwrap_or_else(|| "n/a".to_string())
        );
    }
    eprintln!("r_forecast_parity checks: {}", checks.len());
}
