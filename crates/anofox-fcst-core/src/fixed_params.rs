//! Externally supplied model parameters (#317).
//!
//! Hyper-model forecasting predicts the parameters of a classical model from
//! features and lets the model produce the forecast. These keys fix the
//! parameters of `ETS`, `TSB` and `ARIMA` instead of estimating them:
//!
//! | Model | Keys | Still estimated |
//! |---|---|---|
//! | `ETS` (with a `model` spec) | `alpha`, `beta`, `gamma`, `phi` | initial states, by likelihood |
//! | `TSB` | `alpha_d`, `alpha_p` | initial demand level and probability |
//! | `ARIMA` (with `arima_order`) | `ar_coefficients`, `ma_coefficients`, `seasonal_ar_coefficients`, `seasonal_ma_coefficients`, `intercept` | nothing |
//!
//! The C++ layer passes the raw key/value strings through one FFI field
//! (`key=value` pairs separated by `;`); this module parses them strictly, so
//! an unparsable value is an error rather than a silent default.

use crate::error::{ForecastError, Result};
use crate::forecast::ModelType;

/// Fixed parameters for `ETS`, `TSB` and `ARIMA`. `None` = not supplied.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct FixedParams {
    /// ETS level smoothing.
    pub alpha: Option<f64>,
    /// ETS trend smoothing.
    pub beta: Option<f64>,
    /// ETS seasonal smoothing.
    pub gamma: Option<f64>,
    /// ETS damping.
    pub phi: Option<f64>,
    /// TSB demand-size smoothing.
    pub alpha_d: Option<f64>,
    /// TSB demand-probability smoothing.
    pub alpha_p: Option<f64>,
    /// ARIMA non-seasonal AR coefficients (length `p`).
    pub ar: Option<Vec<f64>>,
    /// ARIMA non-seasonal MA coefficients (length `q`).
    pub ma: Option<Vec<f64>>,
    /// ARIMA seasonal AR coefficients (length `P`).
    pub sar: Option<Vec<f64>>,
    /// ARIMA seasonal MA coefficients (length `Q`).
    pub sma: Option<Vec<f64>>,
    /// ARIMA mean of the differenced series (R `Arima`'s `intercept` / drift).
    pub intercept: Option<f64>,
}

/// The SQL keys this module owns, in documentation order.
pub const FIXED_PARAM_KEYS: [&str; 11] = [
    "alpha",
    "beta",
    "gamma",
    "phi",
    "alpha_d",
    "alpha_p",
    "ar_coefficients",
    "ma_coefficients",
    "seasonal_ar_coefficients",
    "seasonal_ma_coefficients",
    "intercept",
];

impl FixedParams {
    /// Any ETS smoothing parameter supplied.
    pub fn has_ets(&self) -> bool {
        self.alpha.is_some() || self.beta.is_some() || self.gamma.is_some() || self.phi.is_some()
    }

    /// Any TSB smoothing parameter supplied.
    pub fn has_tsb(&self) -> bool {
        self.alpha_d.is_some() || self.alpha_p.is_some()
    }

    /// Any ARIMA coefficient or the intercept supplied.
    pub fn has_arima(&self) -> bool {
        self.ar.is_some()
            || self.ma.is_some()
            || self.sar.is_some()
            || self.sma.is_some()
            || self.intercept.is_some()
    }

    /// Names of the supplied keys, for error messages.
    fn supplied(&self) -> Vec<&'static str> {
        let flags = [
            self.alpha.is_some(),
            self.beta.is_some(),
            self.gamma.is_some(),
            self.phi.is_some(),
            self.alpha_d.is_some(),
            self.alpha_p.is_some(),
            self.ar.is_some(),
            self.ma.is_some(),
            self.sar.is_some(),
            self.sma.is_some(),
            self.intercept.is_some(),
        ];
        FIXED_PARAM_KEYS
            .iter()
            .zip(flags)
            .filter(|(_, set)| *set)
            .map(|(k, _)| *k)
            .collect()
    }
}

fn invalid(param: &str, value: &str, reason: impl Into<String>) -> ForecastError {
    ForecastError::InvalidParameter {
        param: param.to_string(),
        value: value.to_string(),
        reason: reason.into(),
    }
}

fn parse_number(key: &str, raw: &str) -> Result<f64> {
    let v: f64 = raw
        .trim()
        .parse()
        .map_err(|_| invalid(key, raw, "must be a number"))?;
    if !v.is_finite() {
        return Err(invalid(key, raw, "must be finite"));
    }
    Ok(v)
}

/// `[0.5, -0.2]` / `0.5,-0.2` / `[]` → coefficients.
fn parse_list(key: &str, raw: &str) -> Result<Vec<f64>> {
    let inner = raw
        .trim()
        .trim_start_matches('[')
        .trim_end_matches(']')
        .trim();
    if inner.is_empty() {
        return Ok(vec![]);
    }
    inner
        .split(',')
        .map(|item| parse_number(key, item))
        .collect()
}

/// Parses the packed `key=value;key=value` string from the FFI layer.
/// Empty input = no fixed parameters.
pub fn parse_fixed_params(packed: &str) -> Result<FixedParams> {
    let mut fp = FixedParams::default();
    for pair in packed.split(';').filter(|p| !p.trim().is_empty()) {
        let (key, raw) = pair
            .split_once('=')
            .ok_or_else(|| invalid("params", pair, "expected key=value"))?;
        let key = key.trim();
        match key {
            "alpha" => fp.alpha = Some(parse_number(key, raw)?),
            "beta" => fp.beta = Some(parse_number(key, raw)?),
            "gamma" => fp.gamma = Some(parse_number(key, raw)?),
            "phi" => fp.phi = Some(parse_number(key, raw)?),
            "alpha_d" => fp.alpha_d = Some(parse_number(key, raw)?),
            "alpha_p" => fp.alpha_p = Some(parse_number(key, raw)?),
            "ar_coefficients" => fp.ar = Some(parse_list(key, raw)?),
            "ma_coefficients" => fp.ma = Some(parse_list(key, raw)?),
            "seasonal_ar_coefficients" => fp.sar = Some(parse_list(key, raw)?),
            "seasonal_ma_coefficients" => fp.sma = Some(parse_list(key, raw)?),
            "intercept" => fp.intercept = Some(parse_number(key, raw)?),
            other => return Err(invalid(other, raw, "unknown fixed parameter")),
        }
    }
    Ok(fp)
}

/// Each family's keys are valid only with its own model; `ETS` keys also
/// need an explicit `model` spec (without one, 'ETS' runs AutoETS selection,
/// which has nothing to fix). A key the model would ignore is an error.
pub fn validate_fixed_params(
    model: ModelType,
    ets_spec: Option<&str>,
    fp: &FixedParams,
) -> Result<()> {
    let allowed: &[&str] = match model {
        ModelType::ETS => &FIXED_PARAM_KEYS[0..4],
        ModelType::TSB => &FIXED_PARAM_KEYS[4..6],
        ModelType::ARIMA => &FIXED_PARAM_KEYS[6..11],
        _ => &[],
    };
    if let Some(key) = fp.supplied().into_iter().find(|k| !allowed.contains(k)) {
        let owner = match FIXED_PARAM_KEYS.iter().position(|k| *k == key) {
            Some(0..=3) => "method='ETS' with a model spec",
            Some(4..=5) => "method='TSB'",
            _ => "method='ARIMA'",
        };
        return Err(invalid(
            key,
            model.name(),
            format!("only valid with {owner}"),
        ));
    }
    if model == ModelType::ETS && fp.has_ets() && ets_spec.is_none() {
        return Err(invalid(
            "alpha",
            "ETS",
            "fixed ETS parameters need an explicit model spec (e.g. model: 'AAA'); \
             without one, 'ETS' runs AutoETS selection",
        ));
    }
    if model == ModelType::ETS && fp.has_ets() && fp.alpha.is_none() {
        return Err(invalid(
            "alpha",
            "ETS",
            "required when fixing ETS parameters",
        ));
    }
    if model == ModelType::TSB {
        for (key, v) in [("alpha_d", fp.alpha_d), ("alpha_p", fp.alpha_p)] {
            if let Some(v) = v {
                if !(0.01..=0.99).contains(&v) {
                    return Err(invalid(key, &v.to_string(), "must lie in [0.01, 0.99]"));
                }
            }
        }
    }
    Ok(())
}

/// Validates fixed ARIMA coefficients against the order: every list must
/// have exactly the order's length (an omitted list counts as empty).
pub fn check_arima_coefficients(
    fp: &FixedParams,
    [p, _d, q]: [usize; 3],
    seasonal: Option<[usize; 3]>,
) -> Result<()> {
    let [cap_p, _, cap_q] = seasonal.unwrap_or([0, 0, 0]);
    for (key, list, order, order_name) in [
        ("ar_coefficients", &fp.ar, p, "p"),
        ("ma_coefficients", &fp.ma, q, "q"),
        ("seasonal_ar_coefficients", &fp.sar, cap_p, "P"),
        ("seasonal_ma_coefficients", &fp.sma, cap_q, "Q"),
    ] {
        let len = list.as_ref().map_or(0, Vec::len);
        if len != order {
            return Err(invalid(
                key,
                &format!("{len} values"),
                format!("needs exactly {order_name} = {order} values for this order"),
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_packed_values() {
        let fp = parse_fixed_params(
            "alpha=0.3;beta=0.05;ar_coefficients=[0.5, -0.2];ma_coefficients=[];intercept=-1.5",
        )
        .unwrap();
        assert_eq!(fp.alpha, Some(0.3));
        assert_eq!(fp.beta, Some(0.05));
        assert_eq!(fp.ar, Some(vec![0.5, -0.2]));
        assert_eq!(fp.ma, Some(vec![]));
        assert_eq!(fp.intercept, Some(-1.5));
        assert_eq!(parse_fixed_params("").unwrap(), FixedParams::default());
    }

    #[test]
    fn unparsable_values_are_errors() {
        for bad in [
            "alpha=abc",
            "alpha=NaN",
            "ar_coefficients=[0.5, x]",
            "alpha",
            "bogus=1",
        ] {
            assert!(parse_fixed_params(bad).is_err(), "{bad} parsed");
        }
    }

    #[test]
    fn keys_belong_to_their_model() {
        let ets = FixedParams {
            alpha: Some(0.3),
            ..Default::default()
        };
        assert!(validate_fixed_params(ModelType::ETS, Some("ANN"), &ets).is_ok());
        let msg = validate_fixed_params(ModelType::ETS, None, &ets)
            .unwrap_err()
            .to_string();
        assert!(msg.contains("model spec"), "{msg}");
        let msg = validate_fixed_params(ModelType::AutoETS, None, &ets)
            .unwrap_err()
            .to_string();
        assert!(msg.contains("method='ETS'"), "{msg}");

        let tsb = FixedParams {
            alpha_d: Some(1.2),
            ..Default::default()
        };
        assert!(validate_fixed_params(ModelType::TSB, None, &tsb).is_err());
        let ar = FixedParams {
            ar: Some(vec![0.5]),
            ..Default::default()
        };
        assert!(validate_fixed_params(ModelType::TSB, None, &ar).is_err());
        assert!(check_arima_coefficients(&ar, [1, 0, 0], None).is_ok());
        assert!(check_arima_coefficients(&ar, [2, 0, 0], None).is_err());
        assert!(check_arima_coefficients(&ar, [1, 0, 1], None).is_err());
    }
}
