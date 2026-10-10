//! R-faithful port of `stats::supsmu` (Friedman's 1984 "super smoother"),
//! restricted to the call shape R's own `stats::supsmu(seq_len(n), x)`
//! wrapper uses: `iper = 1` (non-periodic `x`), `span = 0` ("cv" — automatic
//! three-span cross-validated selection), `bass = 0` (no high-frequency
//! penalty), unit weights, and the spline branch disabled
//! (`setsmu(trace = FALSE)`).
//!
//! Ported from R's Fortran source (`src/library/stats/src/ppr.f`, R trunk,
//! fetched 2026-10-10 via
//! `https://raw.githubusercontent.com/wch/r-source/trunk/src/library/stats/src/ppr.f`,
//! subroutines `supsmu` at line 997 and `smooth` at line 1134). Used by
//! `stats::stl_strengths` as the trend estimator for series with no usable
//! seasonal period (forecast::mstl's own non-seasonal fallback, Phase 13
//! plan 13-02, STAT-02/D-02).
//!
//! Because `iper` is always `1` (or `-1`, for the acvr-suppressed internal
//! calls) in this call shape, `jper = iabs(iper)` is always `1`: the
//! Fortran's `iper = 2` ("periodic x") branches are dead code here and are
//! not ported.

use crate::error::{ForecastError, Result};

/// `spans`, `big`, `sml`, `eps` from R's `block data bksupsmu` (`ppr.f`).
const SPANS: [f64; 3] = [0.05, 0.2, 0.5];
const BIG: f64 = 1.0e20;
const EPS: f64 = 1.0e-3;

/// R-faithful `stats::supsmu(x, y)` with `span = "cv"`, `bass = 0`,
/// `periodic = FALSE`. `x` must be strictly increasing with `x.len() ==
/// y.len() >= 4`.
pub fn supsmu(x: &[f64], y: &[f64]) -> Result<Vec<f64>> {
    let n = x.len();
    if y.len() != n {
        return Err(ForecastError::InvalidInput(format!(
            "supsmu: x and y must have equal length, got {} and {}",
            n,
            y.len()
        )));
    }
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }
    for i in 1..n {
        if x[i] <= x[i - 1] {
            return Err(ForecastError::InvalidInput(
                "supsmu: x must be strictly increasing".to_string(),
            ));
        }
    }

    let w = vec![1.0f64; n];

    // Fortran: `if (x(n).gt.x(1)) go to 30` — else boundary case: every
    // output is the weighted mean of y. Unreachable given the strictly-
    // increasing check above (kept for fidelity to the source).
    if x[n - 1] <= x[0] {
        let sy: f64 = (0..n).map(|j| w[j] * y[j]).sum();
        let sw: f64 = w.iter().sum();
        let a = if sw > 0.0 { sy / sw } else { 0.0 };
        return Ok(vec![a; n]);
    }

    // scale = IQR(x) via the widening 1-indexed i/j search (ppr.f:1080-1087).
    let mut i1 = (n / 4).max(1);
    let mut j1 = 3 * i1;
    let mut scale = x[j1 - 1] - x[i1 - 1];
    while scale <= 0.0 {
        if j1 < n {
            j1 += 1;
        }
        if i1 > 1 {
            i1 -= 1;
        }
        scale = x[j1 - 1] - x[i1 - 1];
    }
    let vsmlsq = (EPS * scale).powi(2);

    // --- "cv" (cross-validation over three spans) branch -----------------
    let mut sc1 = vec![0.0f64; n];
    let mut sc2 = vec![0.0f64; n];
    let mut sc3 = vec![0.0f64; n];
    let mut sc4 = vec![0.0f64; n];
    let mut sc5 = vec![0.0f64; n];
    let mut sc6 = vec![0.0f64; n];
    let mut sc7 = vec![0.0f64; n];
    let mut h = vec![0.0f64; n];

    smooth(x, y, &w, SPANS[0], true, vsmlsq, &mut sc1, &mut sc7);
    smooth(x, &sc7, &w, SPANS[1], false, vsmlsq, &mut sc2, &mut h);

    smooth(x, y, &w, SPANS[1], true, vsmlsq, &mut sc3, &mut sc7);
    smooth(x, &sc7, &w, SPANS[1], false, vsmlsq, &mut sc4, &mut h);

    smooth(x, y, &w, SPANS[2], true, vsmlsq, &mut sc5, &mut sc7);
    smooth(x, &sc7, &w, SPANS[1], false, vsmlsq, &mut sc6, &mut h);

    // Select, per point, the span minimizing the (smoothed) leave-one-out
    // residual. `alpha` (bass tone) is always 0 here, so the bass-tone
    // adjustment (`ppr.f:1110-1113`) never triggers.
    for j in 0..n {
        let mut resmin = BIG;
        let mut chosen = SPANS[0];
        for (idx, resid_col) in [&sc2, &sc4, &sc6].iter().enumerate() {
            if resid_col[j] < resmin {
                resmin = resid_col[j];
                chosen = SPANS[idx];
            }
        }
        sc7[j] = chosen;
    }

    smooth(x, &sc7, &w, SPANS[1], false, vsmlsq, &mut sc2, &mut h);

    for j in 0..n {
        sc2[j] = sc2[j].clamp(SPANS[0], SPANS[2]);
        let f = sc2[j] - SPANS[1];
        if f >= 0.0 {
            let f2 = f / (SPANS[2] - SPANS[1]);
            sc4[j] = (1.0 - f2) * sc3[j] + f2 * sc5[j];
        } else {
            let f2 = -f / (SPANS[1] - SPANS[0]);
            sc4[j] = (1.0 - f2) * sc3[j] + f2 * sc1[j];
        }
    }

    let mut smo = vec![0.0f64; n];
    smooth(x, &sc4, &w, SPANS[0], false, vsmlsq, &mut smo, &mut h);
    Ok(smo)
}

/// Fixed-span running weighted linear smoother with an O(n) incremental
/// window (`smooth` in R's C/Fortran source, restricted to the non-periodic
/// `jper = 1` case). `compute_acvr` selects whether the leave-one-out
/// cross-validation residual (`acvr`, only meaningful when `iper > 0` in the
/// Fortran) is computed; `acvr` must still be a valid `n`-length scratch
/// buffer either way (callers reuse it across calls, matching R).
#[allow(clippy::too_many_arguments)]
fn smooth(
    x: &[f64],
    y: &[f64],
    w: &[f64],
    span: f64,
    compute_acvr: bool,
    vsmlsq: f64,
    smo: &mut [f64],
    acvr: &mut [f64],
) {
    let n = x.len();
    let mut ibw = (0.5 * span * n as f64 + 0.5) as i64;
    if ibw < 2 {
        ibw = 2;
    }
    let ibw = ibw as usize;
    let it = (2 * ibw + 1).min(n);

    let mut xm = 0.0f64;
    let mut ym = 0.0f64;
    let mut var = 0.0f64;
    let mut cvar = 0.0f64;
    let mut fbw = 0.0f64;

    for idx in 0..it {
        let xti = x[idx];
        let wt = w[idx];
        let fbo = fbw;
        fbw += wt;
        if fbw > 0.0 {
            xm = (fbo * xm + wt * xti) / fbw;
        }
        if fbw > 0.0 {
            ym = (fbo * ym + wt * y[idx]) / fbw;
        }
        let mut tmp = 0.0;
        if fbo > 0.0 {
            tmp = fbw * wt * (xti - xm) / fbo;
        }
        var += tmp * (xti - xm);
        cvar += tmp * (y[idx] - ym);
    }

    for j in 0..n {
        let j1 = (j + 1) as isize; // 1-indexed position, matching the Fortran `j`
        let out1 = j1 - ibw as isize - 1;
        let in1 = j1 + ibw as isize;
        if out1 >= 1 && (in1 as usize) <= n {
            let out = (out1 - 1) as usize;
            let inn = (in1 - 1) as usize;
            let xto = x[out];
            let xti = x[inn];

            let mut wt = w[out];
            let mut fbo = fbw;
            fbw -= wt;
            let mut tmp = 0.0;
            if fbw > 0.0 {
                tmp = fbo * wt * (xto - xm) / fbw;
            }
            var -= tmp * (xto - xm);
            cvar -= tmp * (y[out] - ym);
            if fbw > 0.0 {
                xm = (fbo * xm - wt * xto) / fbw;
            }
            if fbw > 0.0 {
                ym = (fbo * ym - wt * y[out]) / fbw;
            }

            wt = w[inn];
            fbo = fbw;
            fbw += wt;
            if fbw > 0.0 {
                xm = (fbo * xm + wt * xti) / fbw;
            }
            if fbw > 0.0 {
                ym = (fbo * ym + wt * y[inn]) / fbw;
            }
            tmp = 0.0;
            if fbo > 0.0 {
                tmp = fbw * wt * (xti - xm) / fbo;
            }
            var += tmp * (xti - xm);
            cvar += tmp * (y[inn] - ym);
        }

        let a = if var > vsmlsq { cvar / var } else { 0.0 };
        smo[j] = a * (x[j] - xm) + ym;

        if compute_acvr {
            let mut hh = 0.0;
            if fbw > 0.0 {
                hh = 1.0 / fbw;
            }
            if var > vsmlsq {
                hh += (x[j] - xm).powi(2) / var;
            }
            let a2 = 1.0 - w[j] * hh;
            if a2 > 0.0 {
                acvr[j] = (y[j] - smo[j]).abs() / a2;
            } else if j > 0 {
                acvr[j] = acvr[j - 1];
            } else {
                acvr[j] = 0.0;
            }
        }
    }

    // Recompute fitted values as the weighted mean for runs of non-unique
    // x(.) values (ppr.f:1232-1253).
    let mut j = 0usize;
    while j < n {
        let j0 = j;
        let mut sy = smo[j] * w[j];
        let mut fbw2 = w[j];
        if j < n - 1 {
            while j + 1 < n && x[j + 1] <= x[j] {
                j += 1;
                sy += w[j] * smo[j];
                fbw2 += w[j];
            }
        }
        if j > j0 {
            let a = if fbw2 > 0.0 { sy / fbw2 } else { 0.0 };
            for v in smo.iter_mut().take(j + 1).skip(j0) {
                *v = a;
            }
        }
        j += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_mismatched_lengths() {
        let x = [1.0, 2.0, 3.0, 4.0];
        let y = [1.0, 2.0, 3.0];
        assert!(supsmu(&x, &y).is_err());
    }

    #[test]
    fn rejects_non_increasing_x() {
        let x = [1.0, 2.0, 2.0, 4.0];
        let y = [1.0, 2.0, 3.0, 4.0];
        assert!(supsmu(&x, &y).is_err());
    }

    #[test]
    fn smooths_a_linear_trend_closely() {
        let n = 60;
        let x: Vec<f64> = (0..n).map(|i| (i + 1) as f64).collect();
        let y: Vec<f64> = x.iter().map(|&t| 2.0 * t + 1.0).collect();
        let smoothed = supsmu(&x, &y).unwrap();
        for i in 0..n {
            assert!(
                (smoothed[i] - y[i]).abs() < 1.0,
                "index {i}: smoothed {} vs {}",
                smoothed[i],
                y[i]
            );
        }
    }
}
