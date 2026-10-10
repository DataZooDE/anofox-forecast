//! R-faithful port of `stats::stl` (Cleveland, Cleveland, McRae & Terpenning
//! 1990, "STL: A Seasonal-Trend Decomposition Procedure Based on Loess").
//!
//! Ported from R's own C translation of the original Fortran
//! (`src/library/stats/src/stl.c`, R trunk, fetched 2026-10-10 via
//! `https://raw.githubusercontent.com/wch/r-source/trunk/src/library/stats/src/stl.c`),
//! not from the crate's own `anofox_forecast::seasonality::stl::STL`
//! (0.15.10): that implementation uses a degree-0 kernel smoother with
//! truncated symmetric windows, no cycle-subseries end extension, and no
//! jump interpolation, so it does not reproduce `stats::stl`'s output to the
//! 1e-8 component tolerance `tests/stl_reference.rs` requires. Per D-02
//! ("prefer the crate's STL if correct vs R stats::stl, else core"), this
//! module supplies the R-faithful implementation instead (Phase 13 plan
//! 13-02, STAT-02). Phase 15 DET-04 can reuse it.
//!
//! The port keeps the original (1-indexed, Fortran-derived) loop structure
//! and variable names wherever practical — the 1e-8 fixture tolerance does
//! not admit idiomatic simplification of the loess/subseries/low-pass
//! arithmetic.

use crate::error::{ForecastError, Result};

/// STL configuration, mirroring the Fortran/C `stl()` arguments one-to-one.
#[derive(Debug, Clone, Copy)]
pub struct StlParams {
    /// Seasonal period (`np`); must be >= 2.
    pub period: usize,
    /// Seasonal-smoother span (`s.window`).
    pub s_window: usize,
    /// Seasonal-smoother local degree (`s.degree`): 0 or 1.
    pub s_degree: usize,
    /// Trend-smoother span (`t.window`).
    pub t_window: usize,
    /// Trend-smoother local degree (`t.degree`): 0 or 1.
    pub t_degree: usize,
    /// Low-pass-smoother span (`l.window`).
    pub l_window: usize,
    /// Low-pass-smoother local degree (`l.degree`): 0 or 1.
    pub l_degree: usize,
    /// Seasonal-smoother jump (`s.jump`).
    pub s_jump: usize,
    /// Trend-smoother jump (`t.jump`).
    pub t_jump: usize,
    /// Low-pass-smoother jump (`l.jump`).
    pub l_jump: usize,
    /// Number of inner iterations (`ni`).
    pub inner: usize,
    /// Number of outer (robustness) iterations (`no`).
    pub outer: usize,
}

/// `R`'s `round()` + even/odd bump: `nextodd(x) = round(x)`, `+1` if even.
fn nextodd(x: f64) -> usize {
    let mut r = x.round() as i64;
    if r % 2 == 0 {
        r += 1;
    }
    r.max(1) as usize
}

impl StlParams {
    /// R `stl()` defaults for a numeric (non-`"periodic"`) `s.window`, with
    /// `robust = FALSE` (`stats` 4.6.1, `R/stl.R`):
    ///
    /// - `s.degree = 0`, `t.degree = l.degree = 1`
    /// - `t.window = nextodd(ceiling(1.5 * period / (1 - 1.5 / s.window)))`
    /// - `l.window = nextodd(period)`
    /// - `{s,t,l}.jump = max(1, ceiling({s,t,l}.window / 10))`
    /// - `inner = 2`, `outer = 0`
    pub fn r_default(period: usize, s_window: usize) -> Self {
        let s_window = odd_at_least3(s_window);
        let t_window = nextodd((1.5 * period as f64 / (1.0 - 1.5 / s_window as f64)).ceil());
        let l_window = nextodd(period as f64);
        let s_jump = ((s_window as f64) / 10.0).ceil().max(1.0) as usize;
        let t_jump = ((t_window as f64) / 10.0).ceil().max(1.0) as usize;
        let l_jump = ((l_window as f64) / 10.0).ceil().max(1.0) as usize;
        Self {
            period,
            s_window,
            s_degree: 0,
            t_window,
            t_degree: 1,
            l_window,
            l_degree: 1,
            s_jump,
            t_jump,
            l_jump,
            inner: 2,
            outer: 0,
        }
    }
}

/// STL decomposition result: `trend + seasonal + remainder == values`
/// (within floating-point error) for every index.
#[derive(Debug, Clone)]
pub struct StlComponents {
    pub trend: Vec<f64>,
    pub seasonal: Vec<f64>,
    pub remainder: Vec<f64>,
}

fn odd_at_least3(w: usize) -> usize {
    let mut v = w.max(3);
    if v.is_multiple_of(2) {
        v += 1;
    }
    v
}

/// R-faithful STL decomposition. Errors with [`ForecastError::InvalidInput`]
/// for `period < 2`, and [`ForecastError::InsufficientData`] for
/// `n <= 2 * period` — matching R's `stl()` (`length(x) <= 2 * frequency(x)`
/// error / `tsfeatures::stl_features`'s "insufficient data" treatment).
pub fn stl(values: &[f64], params: &StlParams) -> Result<StlComponents> {
    let n = values.len();
    if params.period < 2 {
        return Err(ForecastError::InvalidInput(format!(
            "stl: period must be >= 2, got {}",
            params.period
        )));
    }
    if n <= 2 * params.period {
        return Err(ForecastError::InsufficientData {
            needed: 2 * params.period + 1,
            got: n,
        });
    }

    let np = params.period.max(2);
    let new_ns = odd_at_least3(params.s_window);
    let new_nt = odd_at_least3(params.t_window);
    let new_nl = odd_at_least3(params.l_window);
    let n2p = n + 2 * np;

    let mut rw = vec![1.0f64; n];
    let mut season = vec![0.0f64; n];
    let mut trend = vec![0.0f64; n];
    // WR-04 fix: 6 (not 5) scratch buffers of n2p -- stlss now gets its own
    // dedicated scratch instead of aliasing stlstp's `season` output parameter.
    let mut work = vec![0.0f64; 6 * n2p];

    let mut use_rw = false;
    let mut k = 0usize;
    loop {
        stlstp(
            values,
            np,
            new_ns,
            new_nt,
            new_nl,
            params.s_degree,
            params.t_degree,
            params.l_degree,
            params.s_jump,
            params.t_jump,
            params.l_jump,
            params.inner,
            use_rw,
            &rw,
            &mut season,
            &mut trend,
            &mut work,
        );
        k += 1;
        if k > params.outer {
            break;
        }
        let fit: Vec<f64> = (0..n).map(|i| trend[i] + season[i]).collect();
        stlrwt(values, &fit, &mut rw);
        use_rw = true;
    }
    if params.outer == 0 {
        for w in rw.iter_mut() {
            *w = 1.0;
        }
    }

    let remainder: Vec<f64> = (0..n).map(|i| values[i] - trend[i] - season[i]).collect();

    Ok(StlComponents {
        trend,
        seasonal: season,
        remainder,
    })
}

/// One pass of the STL "inner loop" (`stlstp` in R's C source): `niter`
/// repetitions of (1) cycle-subseries smoothing + low-pass filtering to
/// extract `season`, then (2) smoothing the deseasonalized series to extract
/// `trend`.
#[allow(clippy::too_many_arguments)]
fn stlstp(
    y: &[f64],
    np: usize,
    ns: usize,
    nt: usize,
    nl: usize,
    isdeg: usize,
    itdeg: usize,
    ildeg: usize,
    nsjump: usize,
    ntjump: usize,
    nljump: usize,
    niter: usize,
    use_rw: bool,
    rw: &[f64],
    season: &mut [f64],
    trend: &mut [f64],
    work: &mut [f64],
) {
    let n = y.len();
    let n2p = n + 2 * np;
    debug_assert_eq!(work.len(), 6 * n2p);

    // WR-04 fix: `stlss` gets its OWN scratch buffer (`buf5`), separate from
    // `season` (this function's own output parameter, eventually overwritten
    // below with the real seasonal estimate). Previously `season` doubled as
    // `stlss`'s `work4` scratch -- correct only because nothing in this loop
    // read `season`'s contents between the `stlss` call and the final
    // assignment, an aliasing invariant enforced by convention, not the type
    // system. `work` is now `6 * n2p` (was `5 * n2p`) to make room.
    let (buf0, rest) = work.split_at_mut(n2p);
    let (buf1, rest) = rest.split_at_mut(n2p);
    let (buf2, rest) = rest.split_at_mut(n2p);
    let (buf3, rest) = rest.split_at_mut(n2p);
    let (buf4, buf5) = rest.split_at_mut(n2p);

    for _ in 0..niter {
        for i in 0..n {
            buf0[i] = y[i] - trend[i];
        }
        stlss(
            &buf0[..n],
            np,
            ns,
            isdeg,
            nsjump,
            use_rw,
            rw,
            buf1,
            buf2,
            buf3,
            buf4,
            buf5,
        );
        stlfts(buf1, np, buf2, buf0);
        stless(&buf2[..n], nl, ildeg, nljump, false, buf3, buf0, buf4);
        for i in 0..n {
            season[i] = buf1[np + i] - buf0[i];
        }
        for i in 0..n {
            buf0[i] = y[i] - season[i];
        }
        stless(&buf0[..n], nt, itdeg, ntjump, use_rw, rw, trend, buf2);
    }
}

/// Cycle-subseries smoothing + end extension (`stlss` in R's C source):
/// splits `y` (length `n`, the detrended series for this inner iteration)
/// into `np` cycle-subseries, smooths each with [`stless`]/[`stlest`], and
/// extrapolates one point at each end, writing the length-`(n + 2*np)`
/// extended seasonal estimate into `season`.
#[allow(clippy::too_many_arguments)]
fn stlss(
    y: &[f64],
    np: usize,
    ns: usize,
    isdeg: usize,
    nsjump: usize,
    use_rw: bool,
    rw: &[f64],
    season: &mut [f64],
    work1: &mut [f64],
    work2: &mut [f64],
    work3: &mut [f64],
    work4: &mut [f64],
) {
    let n = y.len();
    for j in 0..np {
        let k = (n - (j + 1)) / np + 1;
        for i in 0..k {
            work1[i] = y[i * np + j];
        }
        if use_rw {
            for i in 0..k {
                work3[i] = rw[i * np + j];
            }
        }
        stless(
            &work1[..k],
            ns,
            isdeg,
            nsjump,
            use_rw,
            &work3[..k],
            &mut work2[1..1 + k],
            work4,
        );
        let nright = ns.min(k);
        match stlest(
            &work1[..k],
            ns,
            isdeg,
            0.0,
            1,
            nright,
            work4,
            use_rw,
            &work3[..k],
        ) {
            Some(v) => work2[0] = v,
            None => work2[0] = work2[1],
        }
        let nleft = ((k as isize) - (ns as isize) + 1).max(1) as usize;
        match stlest(
            &work1[..k],
            ns,
            isdeg,
            (k + 1) as f64,
            nleft,
            k,
            work4,
            use_rw,
            &work3[..k],
        ) {
            Some(v) => work2[k + 1] = v,
            None => work2[k + 1] = work2[k],
        }
        for m in 0..(k + 2) {
            season[m * np + j] = work2[m];
        }
    }
}

/// Three moving averages (spans `np`, `np`, `3`) composing the STL low-pass
/// filter (`stlfts` in R's C source). `x` has length `n2p = n + 2*np`;
/// `trend` (output) and `work` (scratch) must have the same capacity (only
/// a decreasing-length prefix of each is written/read, matching R exactly).
fn stlfts(x: &[f64], np: usize, trend: &mut [f64], work: &mut [f64]) {
    stlma(x, np, trend);
    let len1 = x.len() - np + 1;
    stlma(&trend[..len1], np, work);
    let len2 = len1 - np + 1;
    stlma(&work[..len2], 3, trend);
}

/// Moving average / running mean (`stlma` in R's C source):
/// `ave[i] = mean(x[i..i+len])` for `i` in `0..(x.len() - len + 1)`.
fn stlma(x: &[f64], len: usize, ave: &mut [f64]) {
    let flen = len as f64;
    let mut v: f64 = x[..len].iter().sum();
    ave[0] = v / flen;
    let n = x.len();
    let newn = n - len + 1;
    if newn > 1 {
        // k = len + j - 1 (the entering element), m = j - 1 (the leaving
        // element): the sliding-window add/remove, re-expressed as direct
        // functions of `j` rather than separately incremented counters.
        for j in 1..newn {
            v += x[len + j - 1] - x[j - 1];
            ave[j] = v / flen;
        }
    }
}

/// Robustness weights (`stlrwt` in R's C source): Tukey's biweight on the
/// absolute residuals, scaled by `6 * MAD` (median absolute deviation).
/// Only invoked when `params.outer > 0`.
fn stlrwt(y: &[f64], fit: &[f64], rw: &mut [f64]) {
    let n = y.len();
    let abs_resid: Vec<f64> = (0..n).map(|i| (y[i] - fit[i]).abs()).collect();
    let mid0 = n / 2;
    let mid1 = n - 1 - mid0;
    let mut sorted = abs_resid.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let cmad = (sorted[mid0] + sorted[mid1]) * 3.0;
    let c9 = cmad * 0.999;
    let c1 = cmad * 0.001;
    for i in 0..n {
        let r = abs_resid[i];
        if r <= c1 {
            rw[i] = 1.0;
        } else if r <= c9 {
            let d2 = r / cmad;
            let i_d2 = 1.0 - d2 * d2;
            rw[i] = i_d2 * i_d2;
        } else {
            rw[i] = 0.0;
        }
    }
}

/// Local loess smoother over equally spaced integer abscissas, with the
/// "jump" linear-interpolation optimization (`stless` in R's C source).
/// `ys` (output) and `y` (input) both have length `n = y.len()`; `res`
/// (scratch) must have capacity >= `n`.
#[allow(clippy::too_many_arguments)]
fn stless(
    y: &[f64],
    len: usize,
    ideg: usize,
    njump: usize,
    use_rw: bool,
    rw: &[f64],
    ys: &mut [f64],
    res: &mut [f64],
) {
    let n = y.len();
    if n < 2 {
        ys[0] = y[0];
        return;
    }

    let newnj = njump.min(n - 1).max(1);
    let mut nleft: usize;
    let mut nright: usize;

    if len >= n {
        nleft = 1;
        nright = n;
        let mut i = 0usize;
        while i < n {
            ys[i] = match stlest(y, len, ideg, (i + 1) as f64, nleft, nright, res, use_rw, rw) {
                Some(v) => v,
                None => y[i],
            };
            i += newnj;
        }
    } else {
        let nsh = len.div_ceil(2);
        if newnj == 1 {
            nleft = 1;
            nright = len;
            for i in 0..n {
                if i + 1 > nsh && nright != n {
                    nleft += 1;
                    nright += 1;
                }
                ys[i] = match stlest(y, len, ideg, (i + 1) as f64, nleft, nright, res, use_rw, rw) {
                    Some(v) => v,
                    None => y[i],
                };
            }
        } else {
            nleft = 1;
            nright = len;
            let mut i = 0usize;
            while i < n {
                if i + 1 < nsh {
                    nleft = 1;
                    nright = len;
                } else if i >= n - nsh {
                    nleft = n - len + 1;
                    nright = n;
                } else {
                    nleft = i + 1 - nsh + 1;
                    nright = len + i + 1 - nsh;
                }
                ys[i] = match stlest(y, len, ideg, (i + 1) as f64, nleft, nright, res, use_rw, rw) {
                    Some(v) => v,
                    None => y[i],
                };
                i += newnj;
            }
        }
    }

    if newnj != 1 {
        let mut i = 0usize;
        while i < n - newnj {
            let delta = (ys[i + newnj] - ys[i]) / newnj as f64;
            for j in (i + 1)..(i + newnj) {
                ys[j] = ys[i] + delta * (j - i) as f64;
            }
            i += newnj;
        }
        let k = (n - 1) / newnj * newnj;
        if k != n - 1 {
            ys[n - 1] = match stlest(y, len, ideg, n as f64, nleft, nright, res, use_rw, rw) {
                Some(v) => v,
                None => y[n - 1],
            };
            let delta = (ys[n - 1] - ys[k]) / (n - 1 - k) as f64;
            for j in (k + 1)..(n - 1) {
                ys[j] = ys[k] + delta * (j - k) as f64;
            }
        }
    }
}

/// Single-point weighted local loess fit (`stlest` in R's C source):
/// tricube-weighted regression of degree `ideg` (0 or 1) over
/// `y[nleft-1..nright]` (1-indexed `nleft`/`nright`, inclusive), evaluated
/// at abscissa `xs`. Returns `None` when the total weight is non-positive
/// (R's `ok = .false.`, caller falls back to the raw observation).
// `j` indexes `w`, `y` and (conditionally) `rw` together within the
// `(nleft-1)..nright` window — a direct 1:1 port of the Fortran index
// arithmetic; rewriting as iterator chains over three co-indexed slices
// with a non-zero start offset would obscure the correspondence to
// `stlest` in R's C source without fixing a real issue.
#[allow(clippy::too_many_arguments, clippy::needless_range_loop)]
fn stlest(
    y: &[f64],
    len: usize,
    ideg: usize,
    xs: f64,
    nleft: usize,
    nright: usize,
    w: &mut [f64],
    use_rw: bool,
    rw: &[f64],
) -> Option<f64> {
    let n = y.len();
    let range = (n - 1) as f64;
    let mut h = (xs - nleft as f64).max(nright as f64 - xs);
    if len > n {
        h += ((len - n) / 2) as f64;
    }
    let h9 = h * 0.999;
    let h1 = h * 0.001;
    let mut a = 0.0;
    for j in (nleft - 1)..nright {
        let r = ((j + 1) as f64 - xs).abs();
        if r <= h9 {
            if r <= h1 {
                w[j] = 1.0;
            } else {
                let ratio = r / h;
                w[j] = (1.0 - ratio * ratio * ratio).powi(3);
            }
            if use_rw {
                w[j] *= rw[j];
            }
            a += w[j];
        } else {
            w[j] = 0.0;
        }
    }

    if a <= 0.0 {
        return None;
    }

    for j in (nleft - 1)..nright {
        w[j] /= a;
    }

    if h > 0.0 && ideg > 0 {
        let mut aa = 0.0;
        for j in (nleft - 1)..nright {
            aa += w[j] * (j + 1) as f64;
        }
        let b = xs - aa;
        let mut c = 0.0;
        for j in (nleft - 1)..nright {
            let d = (j + 1) as f64 - aa;
            c += w[j] * d * d;
        }
        if c.sqrt() > range * 0.001 {
            let bb = b / c;
            for j in (nleft - 1)..nright {
                w[j] *= bb * ((j + 1) as f64 - aa) + 1.0;
            }
        }
    }

    let mut ys = 0.0;
    for j in (nleft - 1)..nright {
        ys += w[j] * y[j];
    }
    Some(ys)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_period_below_2() {
        let values: Vec<f64> = (0..20).map(|i| i as f64).collect();
        let params = StlParams::r_default(1, 11);
        let err = stl(&values, &params).unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
    }

    #[test]
    fn rejects_insufficient_data() {
        let values: Vec<f64> = (0..20).map(|i| i as f64).collect();
        let params = StlParams::r_default(12, 11);
        let err = stl(&values, &params).unwrap_err();
        assert!(matches!(err, ForecastError::InsufficientData { .. }));
    }

    #[test]
    fn r_default_matches_known_windows() {
        // period 12, s.window 11 -> t.window 21, l.window 13, jumps 2/3/2
        // (R stl()$win / $jump, see validation/reference/README.md section 4).
        let p = StlParams::r_default(12, 11);
        assert_eq!(p.t_window, 21);
        assert_eq!(p.l_window, 13);
        assert_eq!((p.s_jump, p.t_jump, p.l_jump), (2, 3, 2));

        // period 7, s.window 11 -> t.window 13, l.window 7, jumps 2/2/1
        let p7 = StlParams::r_default(7, 11);
        assert_eq!(p7.t_window, 13);
        assert_eq!(p7.l_window, 7);
        assert_eq!((p7.s_jump, p7.t_jump, p7.l_jump), (2, 2, 1));
    }

    #[test]
    fn decomposition_reconstructs_input() {
        let n = 60;
        let values: Vec<f64> = (0..n)
            .map(|i| {
                let t = i as f64;
                10.0 + 0.1 * t + 3.0 * (2.0 * std::f64::consts::PI * t / 12.0).sin()
            })
            .collect();
        let params = StlParams::r_default(12, 11);
        let fit = stl(&values, &params).unwrap();
        for (i, (((t, s), r), v)) in fit
            .trend
            .iter()
            .zip(fit.seasonal.iter())
            .zip(fit.remainder.iter())
            .zip(values.iter())
            .enumerate()
        {
            let reconstructed = t + s + r;
            assert!(
                (reconstructed - v).abs() < 1e-9,
                "index {i}: reconstructed {reconstructed} vs {v}"
            );
        }
    }
}
