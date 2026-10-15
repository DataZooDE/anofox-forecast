# R forecast reference generator (TEST-01).
#
# Writes validation/reference/fixtures/r_forecast/air_passengers.json and
# validation/reference/fixtures/r_forecast/simulated_series.json.
#
# Regenerate with:
#   Rscript validation/reference/r/gen_forecast_reference.R
# (works from any cwd — the script resolves its own directory via --file=).

# common.R is not loaded yet, so the script-dir resolution is inlined here.
.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages(library(forecast))

GENERATOR_PATH <- "validation/reference/r/gen_forecast_reference.R"

series <- reference_series()
air <- series$air
rw <- series$rw
inter <- series$inter
trend <- series$trend
# Deterministic level shift of the existing seeded ar1 series (D-09, FCST-04:
# non-seasonal arima_order parity targets). Consumes no RNG draws, so rw/inter
# and air_passengers.json stay byte-identical to the pre-existing fixture.
ar1_l10 <- 10 + series$ar1

h <- 12
levels <- c(80, 95)

# D-12 root-cause table (17-03 Task 1). Every row's `checks` must stay a
# list() (not c()), even for a single-element row: jsonlite's auto_unbox
# would otherwise collapse a length-1 character vector to a bare string
# instead of a one-element JSON array.
REF_ROOT_CAUSES <- list(
  list(
    id = "RC-01",
    checks = list("air/holt/Holt", "rw/holt/Holt", "trend/holt/Holt"),
    location = "extension",
    cause = paste(
      "Wrapper's SQL 'Holt' dispatched to HoltLinearTrend::auto() (SSE",
      "objective, trend seeded from a single first difference) instead of",
      "ETS(A,A,N), R holt()'s actual definition; the R-faithful ETS(A,A,N)",
      "construction cuts the point deviation 6.86x on AirPassengers and",
      "13.41x on rw, and brings trend into the parity tier (3.01x cut)."
    ),
    evidence = paste(
      "E4 mapping check (diagnosis/harness-output.txt): air",
      "HoltLinearTrend point_dev=1.155e-1 interval_dev=2.312e-1 ->",
      "ETS(A,A,N) R-faithful point_dev=1.684e-2 interval_dev=3.334e-2",
      "(cut 6.86x); rw HoltLinearTrend point_dev=7.517e-2",
      "interval_dev=3.262e-1 -> ETS(A,A,N) R-faithful point_dev=5.604e-3",
      "interval_dev=1.960e-2 (cut 13.41x); trend HoltLinearTrend",
      "point_dev=1.653e-3 interval_dev=2.483e-1 -> ETS(A,A,N) R-faithful",
      "point_dev=5.495e-4 interval_dev=6.532e-3 (cut 3.01x, parity tier)."
    ),
    resolution = paste(
      "fixed: forecast() dispatch and build_forecaster remapped SQL",
      "'Holt' from HoltLinearTrend::auto() to ETS::new(ETSSpec::aan(), 1),",
      "matching R's holt() definition; model_name stays 'Holt' (17-03",
      "Task 1)."
    ),
    upstream = ""
  ),
  list(
    id = "RC-02",
    checks = list(
      "air/holt/ETS:AAN",
      "rw/holt/ETS:AAN",
      "air/hw_additive/HoltWinters",
      "air/hw_additive/ETS:AAA",
      "air/ets_AAA/ETS:AAA",
      "air/ets_MAM/ETS:MAM",
      "air/hw_multiplicative/ETS:MAM"
    ),
    location = "reference",
    cause = paste(
      "At R's own fitted parameters the crate's ETS recursion, likelihood",
      "and bounds agree with R to floating-point precision (E5); the",
      "residual gap is R's own Nelder-Mead optimiser stopping at an",
      "inferior, in-bounds local optimum relative to the crate's own",
      "optimiser on the identical objective (E6)."
    ),
    evidence = paste(
      "E5 (diagnosis/harness-output.txt): loglik_rel_dev <= 8.3e-16,",
      "point_rel_dev <= 1.4e-15 on every check in this row (bit-identical",
      "recursion). E6: air/holt crate_loglik=-863.582556 >",
      "r_loglik=-863.647300 (gap 0.0647); rw/holt",
      "crate_loglik=-515.765392 > r_loglik=-516.366200 (gap 0.6008);",
      "air/hw_additive & air/ets_AAA crate_loglik=-718.500709 >",
      "r_loglik=-765.935848 (gap 47.44); air/ets_MAM &",
      "air/hw_multiplicative crate_loglik=-681.105288 >",
      "r_loglik=-682.403619 / -685.827096 (gap 1.30 / 4.72); in every",
      "case the crate's own parameters stay within R's documented bounds",
      "(alpha/beta/gamma in [1e-4,0.9999], beta<=alpha, gamma<=1-alpha)."
    ),
    resolution = paste(
      "known gap: the crate reaches a better-scoring, in-bounds optimum",
      "than R on these AirPassengers / rw fits; nothing to fix extension-",
      "or crate-side (R itself is the looser optimum)."
    ),
    upstream = ""
  ),
  list(
    id = "RC-03",
    checks = list("air/ets_auto/AutoETS"),
    location = "crate",
    cause = paste(
      "AutoETS's model pool selects ETS(M,A,M) instead of R's",
      "ETS(M,Ad,M) because the crate's own optimiser, at R's selected",
      "(M,Ad,M) spec, reaches a worse log-likelihood than R under the",
      "same objective -- the crate's Nelder-Mead multi-start misses an",
      "optimum R's optimiser finds."
    ),
    evidence = paste(
      "E7 (diagnosis/harness-output.txt): crate_aicc=1409.4003 vs",
      "r_aicc=1400.6384 for MAdM (diff +8.76, worse), while",
      "crate_aicc=1401.0677 for MAM beats r_aicc=1403.6644 for the same",
      "spec. E6 at R's (M,Ad,M) parameters: crate_loglik=-683.964133 <",
      "r_loglik=-679.583216 (gap -4.3809, crate strictly worse under its",
      "own objective), with alpha=0.8024 beta=0.0197 gamma=0.1976",
      "phi=0.98 all inside R's bounds. Reproduced standalone in",
      "repro_rc_03 (r_parity_diagnosis.rs)."
    ),
    resolution = "known gap: upstream optimiser issue, drafted.",
    upstream = "draft: RC-03-autoets-madm-optimum.md"
  ),
  list(
    id = "RC-04",
    checks = list("inter/ses/SESOptimized"),
    location = "crate",
    cause = paste(
      "SimpleExponentialSmoothing::auto() on the zero-inflated",
      "intermittent series converges to a different (alpha, l0) optimum",
      "than R's ses(): the crate's own optimiser, evaluated at R's",
      "fitted parameters, finds a strictly better log-likelihood than",
      "its own converged fit, so the crate's optimiser is missing an",
      "optimum its own objective scores higher."
    ),
    evidence = paste(
      "E5 at R's params (alpha=0.0001, l0=1.2417): loglik_rel_dev=0.000e0,",
      "point_rel_dev=6.437e-15 (bit-identical recursion/likelihood",
      "formula). E6: crate's own fit alpha=0.139114 l0 fixed to",
      "values[0]=4 gives crate_loglik=-380.625612, which is WORSE than",
      "r_loglik=-374.538408 at R's own parameters (gap -6.087205), so the",
      "crate's optimiser converges to a lower-likelihood point than one",
      "it can itself evaluate as better. Reproduced standalone in",
      "repro_rc_04 (r_parity_diagnosis.rs)."
    ),
    resolution = paste(
      "known gap: upstream optimiser issue (likely the crate's",
      "fixed-l0-to-first-observation heuristic for SES on",
      "intermittent/zero-inflated series), drafted."
    ),
    upstream = "draft: RC-04-ses-optimized-intermittent-optimum.md"
  ),
  list(
    id = "RC-05",
    checks = list("air/auto_arima/AutoARIMA"),
    location = "crate",
    cause = paste(
      "SARIMA prediction variance differs from R stats::arima",
      "KalmanForecast: at R's exact selected order (2,1,1)(0,1,0)[12],",
      "explicit ARIMA construction reproduces R's point forecast (rel",
      "dev 6.9e-3) but its 95% half-widths are systematically narrower",
      "than R's state-space prediction-error variance, especially at",
      "longer horizons."
    ),
    evidence = paste(
      "D-15 Miss E8 (diagnosis/harness-output.txt,",
      "autoarima_air_explicit_arima_e8_probe): explicit",
      "ARIMA(2,1,1)(0,1,0)[12] at R's order gives point_dev=6.883204e-3,",
      "interval_dev=2.489813e-1, with crate/R half-width ratio falling",
      "from 1.040 (h=1) to 0.818 (h=6) and recovering only to 0.879",
      "(h=12) -- a systematic underestimate of multi-step SARIMA forecast",
      "variance, not a one-off numerical difference. E3",
      "(autoarima_air_e3_timestamp_probe): synthetic-hourly and",
      "real-monthly timestamps give bit-identical intervals",
      "(same_bits=true), ruling out the wrapper's synthetic index as the",
      "cause. AutoARIMA's own selection path",
      "(autoarima_air_interval_e8_probe) matches the explicit-order call",
      "closely (point ratios near 1.0, interval ratios following the",
      "same h-dependent pattern), so this is an estimation/variance-",
      "formula difference, not a selection difference. Reproduced",
      "standalone in repro_rc_05 (r_parity_diagnosis.rs)."
    ),
    resolution = paste(
      "known gap (user decision D-16, 2026-10-15): this is the ONE",
      "allowed AutoARIMA known gap; order identity and point forecasts",
      "remain hard parity on every series. Being fixed upstream in",
      "parallel (separate worktree/PR targeting anofox-forecast 0.16.2);",
      "the extension takes a follow-up bump to tighten this check once",
      "released."
    ),
    upstream = "draft: RC-05-autoarima-sarima-interval-variance.md"
  )
)

REF_TOLERANCE <- list(
  closed_form_abs = 1e-8,
  model_point_rel = 0.01,
  interval_rel = 0.05,
  root_causes = REF_ROOT_CAUSES,
  auto_arima_selection = list(
    share_abs = 0.10,
    exact_agreement_min = 0.9,
    note = paste(
      "UPST-01 (D-15): share_abs bounds |crate share - R share| for the rw",
      "(0,1,0) share and the ar07 d>=1 share (the UPST-01 claim);",
      "exact_agreement_min is the stricter per-series (p,d,q) agreement",
      "check over all 60 series."
    )
  ),
  notes = paste(
    "closed_form_abs is exact (naive/snaive/rwf drift are closed-form);",
    "model_point_rel and interval_rel are parity targets for later",
    "phases (Phase 10 FCST-01, Phase 17 FCST-09), not asserted in Phase 9."
  )
)
# D-08/D-09/D-12: parity_checks differs per fixture (air's own rows vs
# rw/inter/trend's own rows), so each fixture gets its own tolerance object
# built from the shared REF_TOLERANCE base plus its own parity_checks list.
# root_causes (and everything else in REF_TOLERANCE) stays identical across
# both fixtures -- only parity_checks differs.
#
# 17-03 Task 2 writes ONLY the 9 trend rows (defined where the trend fits
# are available, just above sim_fixture); Task 3 adds the 15 air rows here
# and the 9 rw / 3 inter rows next to SIM's definition.
# 17-03 Task 3: the 15 AirPassengers parity_checks rows. Every
# tolerance/measured value is read from diagnosis/harness-output.txt
# (r_parity_diagnosis.rs) or the 0.16.1 measurement recorded in the plan's
# interfaces block -- never computed here.
AIR_PARITY_CHECKS <- list(
  list(
    series = "air", r_key = "naive", surface = "Naive", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "Naive is closed-form; bounds use the crate's Abramowitz-Stegun 26.2.23 normal-quantile approximation (constant ~2.2e-4 relative half-width error on every series).",
    follow_up = "", measured_point = 0, measured_interval = 2.20e-4
  ),
  list(
    series = "air", r_key = "snaive", surface = "SeasonalNaive", ets_spec = "",
    seasonal_period = 12, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "SeasonalNaive is closed-form; same z approximation as Naive.",
    follow_up = "", measured_point = 0, measured_interval = 2.20e-4
  ),
  list(
    series = "air", r_key = "rwf_drift", surface = "RandomWalkDrift", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-3, status = "parity", cause = "",
    reason = "RandomWalkDrift is closed-form; bounds combine the z approximation with the crate's drift-interval formula, which differs slightly from R's rwf().",
    follow_up = "", measured_point = 4.5e-13, measured_interval = 3.28e-3
  ),
  list(
    series = "air", r_key = "ses", surface = "SESOptimized", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "SimpleExponentialSmoothing::auto() on AirPassengers converges to R ses()'s optimum.",
    follow_up = "", measured_point = 1.65e-9, measured_interval = 2.12e-8
  ),
  list(
    series = "air", r_key = "ses", surface = "ETS:ANN", ets_spec = "ANN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "Same ETS(A,N,N) construction as SESOptimized above.",
    follow_up = "", measured_point = 1.65e-9, measured_interval = 1.56e-8
  ),
  list(
    series = "air", r_key = "holt", surface = "ETS:AAN", ets_spec = "AAN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 0.025,
    interval_tol = 0.05, status = "known_gap", cause = "RC-02",
    reason = "RC-02: R's own Nelder-Mead optimiser stops at an inferior, in-bounds optimum relative to the crate's (E6: crate_loglik=-863.582556 > r_loglik=-863.647300); measured point=1.68e-2 interval=3.33e-2.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 1.68e-2, measured_interval = 3.33e-2
  ),
  list(
    series = "air", r_key = "holt", surface = "Holt", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 0.03,
    interval_tol = 0.05, status = "known_gap", cause = "RC-01,RC-02",
    reason = "D-12/D-13 fix (RC-01) remapped SQL 'Holt' to ETS(A,A,N), cutting the deviation 6.86x; the residual (identical to the ETS:AAN row above) is RC-02's R-optimum-is-looser cause, still outside the parity tier. tol = 1.5x post-fix measured (1.68e-2/3.33e-2), capped at the pre-fix planned tolerance (rel 0.15/0.3).",
    follow_up = "fixed: forecast() dispatch and build_forecaster remapped SQL 'Holt' to ETS::new(ETSSpec::aan(), 1) (17-03 Task 1); residual tracked as RC-02.",
    measured_point = 1.68e-2, measured_interval = 3.33e-2
  ),
  list(
    series = "air", r_key = "hw_additive", surface = "HoltWinters", ets_spec = "",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.3,
    interval_tol = 2.5, status = "known_gap", cause = "RC-02",
    reason = "RC-02: the E4 R-faithful ETS(A,A,A) mapping cuts the deviation only 1.04x (not >= 2x), so no extension fix applies; the residual is R's own optimiser reaching a looser, in-bounds optimum (E6: crate_loglik=-718.500709 > r_loglik=-765.935848). measured point=2.15e-1 interval=1.86.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 2.15e-1, measured_interval = 1.86
  ),
  list(
    series = "air", r_key = "hw_additive", surface = "ETS:AAA", ets_spec = "AAA",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.3,
    interval_tol = 2.5, status = "known_gap", cause = "RC-02",
    reason = "RC-02: same cause as HoltWinters above (identical ETS(A,A,A) construction); measured point=2.06e-1 interval=1.87.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 2.06e-1, measured_interval = 1.87
  ),
  list(
    series = "air", r_key = "hw_multiplicative", surface = "ETS:MAM", ets_spec = "MAM",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.08,
    interval_tol = 1.4, status = "known_gap", cause = "RC-02",
    reason = "RC-02: R's own optimiser stops at an inferior, in-bounds optimum (E6: crate_loglik=-681.105288 > r_loglik=-685.827096); measured point=5.94e-2 interval=1.10.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 5.94e-2, measured_interval = 1.10
  ),
  list(
    series = "air", r_key = "ets_auto", surface = "AutoETS", ets_spec = "",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.07,
    interval_tol = 0.7, status = "known_gap", cause = "RC-03",
    reason = "RC-03: the crate's own optimiser scores R's selected ETS(M,Ad,M) worse than R does (E6: crate_loglik=-683.964133 < r_loglik=-679.583216), so AutoETS selects ETS(M,A,M) instead; measured point=5.14e-2 interval=5.16e-1.",
    follow_up = "draft: RC-03-autoets-madm-optimum.md (pending user filing).",
    measured_point = 5.14e-2, measured_interval = 5.16e-1
  ),
  list(
    series = "air", r_key = "ets_MAM", surface = "ETS:MAM", ets_spec = "MAM",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.1,
    interval_tol = 1.2, status = "known_gap", cause = "RC-02",
    reason = "RC-02: same construction/cause as hw_multiplicative/ETS:MAM above; measured point=7.26e-2 interval=9.03e-1.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 7.26e-2, measured_interval = 9.03e-1
  ),
  list(
    series = "air", r_key = "ets_AAA", surface = "ETS:AAA", ets_spec = "AAA",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 0.3,
    interval_tol = 2.5, status = "known_gap", cause = "RC-02",
    reason = "RC-02: same construction/cause as hw_additive/ETS:AAA above; measured point=2.06e-1 interval=1.87.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 2.06e-1, measured_interval = 1.87
  ),
  list(
    series = "air", r_key = "auto_arima", surface = "AutoARIMA", ets_spec = "",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 6e-4,
    interval_tol = 0.3, status = "known_gap", cause = "RC-05",
    reason = "D-16 (user decision 2026-10-15): order_match=true (R ARIMA(2,1,1)(0,1,0)[12]) and the point forecast is in parity (3.982169e-4), but the 95% interval is outside the parity tier (1.846662e-1) -- the ONE allowed AutoARIMA known gap. Cause RC-05 (crate: SARIMA prediction variance differs from R stats::arima KalmanForecast). tol = 1.5x measured per the D-15 tolerance rule, with no tier cap on this one exception (capping would make the tolerance stricter than the measured value).",
    follow_up = "draft: RC-05-autoarima-sarima-interval-variance.md (pending user filing); being fixed upstream in parallel (separate worktree/PR, anofox-forecast 0.16.2) -- a follow-up bump will tighten this check to the parity tier.",
    measured_point = 3.982169e-4, measured_interval = 1.846662e-1
  ),
  list(
    series = "air", r_key = "thetaf", surface = "Theta", ets_spec = "",
    seasonal_period = 12, point_tol_kind = "rel", point_tol = 1e-5,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "Theta matches R thetaf() to numerical precision on AirPassengers.",
    follow_up = "", measured_point = 3.27e-6, measured_interval = 1.80e-4
  )
)
AIR_TOLERANCE <- c(REF_TOLERANCE, list(parity_checks = AIR_PARITY_CHECKS))

REF_PARAMETERS <- list(horizon = h, levels = levels)

# ---- fit-metadata extractors (feed Phase 10/11 consumers) ----------------

ets_fit_block <- function(fit) {
  list(
    method = fit$method,
    par = as.list(fit$par),
    loglik = as.numeric(fit$loglik),
    aic = as.numeric(fit$aic),
    aicc = as.numeric(fit$aicc),
    bic = as.numeric(fit$bic),
    sigma2 = as.numeric(fit$sigma2)
  )
}

arima_fit_block <- function(fit) {
  list(
    order = as.list(arimaorder(fit)),
    coef = as.list(coef(fit)),
    loglik = as.numeric(fit$loglik),
    aicc = as.numeric(fit$aicc),
    sigma2 = as.numeric(fit$sigma2)
  )
}

# ---- AirPassengers family -------------------------------------------------

ets_auto_fit <- ets(air)
ets_MAM_fit <- ets(air, model = "MAM", damped = FALSE)
ets_AAA_fit <- ets(air, model = "AAA", damped = FALSE)
auto_arima_fit <- auto.arima(air)
arima_airline_fit <- Arima(air, order = c(0, 1, 1), seasonal = c(0, 1, 1))

air_models <- list(
  naive = fc_block(naive(air, h = h, level = levels), "naive(air, h = 12, level = c(80, 95))"),
  snaive = fc_block(snaive(air, h = h, level = levels), "snaive(air, h = 12, level = c(80, 95))"),
  rwf_drift = fc_block(
    rwf(air, h = h, drift = TRUE, level = levels),
    "rwf(air, h = 12, drift = TRUE, level = c(80, 95))"
  ),
  ses = fc_block(ses(air, h = h, level = levels), "ses(air, h = 12, level = c(80, 95))"),
  holt = local({
    f <- holt(air, h = h, level = levels)
    c(fc_block(f, "holt(air, h = 12, level = c(80, 95))"), list(fit = ets_fit_block(f$model)))
  }),
  hw_additive = local({
    f <- hw(air, h = h, level = levels, seasonal = "additive")
    c(
      fc_block(f, "hw(air, h = 12, level = c(80, 95), seasonal = \"additive\")"),
      list(fit = ets_fit_block(f$model))
    )
  }),
  hw_multiplicative = local({
    f <- hw(air, h = h, level = levels, seasonal = "multiplicative")
    c(
      fc_block(f, "hw(air, h = 12, level = c(80, 95), seasonal = \"multiplicative\")"),
      list(fit = ets_fit_block(f$model))
    )
  }),
  ets_auto = c(
    fc_block(forecast(ets_auto_fit, h = h, level = levels), "forecast(ets(air), h = 12, level = c(80, 95))"),
    list(fit = ets_fit_block(ets_auto_fit))
  ),
  ets_MAM = c(
    fc_block(
      forecast(ets_MAM_fit, h = h, level = levels),
      "forecast(ets(air, model = \"MAM\", damped = FALSE), h = 12, level = c(80, 95))"
    ),
    list(fit = ets_fit_block(ets_MAM_fit))
  ),
  ets_AAA = c(
    fc_block(
      forecast(ets_AAA_fit, h = h, level = levels),
      "forecast(ets(air, model = \"AAA\", damped = FALSE), h = 12, level = c(80, 95))"
    ),
    list(fit = ets_fit_block(ets_AAA_fit))
  ),
  auto_arima = c(
    fc_block(
      forecast(auto_arima_fit, h = h, level = levels),
      "forecast(auto.arima(air), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(auto_arima_fit))
  ),
  arima_airline = c(
    fc_block(
      forecast(arima_airline_fit, h = h, level = levels),
      "forecast(Arima(air, order = c(0,1,1), seasonal = c(0,1,1)), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(arima_airline_fit))
  ),
  thetaf = fc_block(thetaf(air, h = h, level = levels), "thetaf(air, h = 12, level = c(80, 95))")
)

ets_defaults_formals <- formals(forecast::ets)
ets_defaults <- list(
  allow.multiplicative.trend = eval(ets_defaults_formals$allow.multiplicative.trend),
  restrict = eval(ets_defaults_formals$restrict),
  bounds = eval(ets_defaults_formals$bounds),
  opt.crit = eval(ets_defaults_formals$opt.crit),
  nmse = eval(ets_defaults_formals$nmse),
  ic = eval(ets_defaults_formals$ic),
  lower = as.numeric(eval(ets_defaults_formals$lower)),
  upper = as.numeric(eval(ets_defaults_formals$upper))
)

errors <- c("A", "M")
trends <- c("N", "A", "Ad")
seasons <- c("N", "A", "M")
ets_candidates <- list()
for (e in errors) {
  for (tr in trends) {
    for (s in seasons) {
      trend_letter <- if (tr == "Ad") "A" else tr
      damped <- tr == "Ad"
      spec <- paste0(e, trend_letter, s)
      key <- paste0(e, tr, s)
      result <- tryCatch(
        {
          fit <- ets(air, model = spec, damped = damped)
          list(
            spec = paste0(e, tr, s),
            fitted = TRUE,
            aicc = as.numeric(fit$aicc),
            loglik = as.numeric(fit$loglik),
            par = as.list(fit$par)
          )
        },
        error = function(err) {
          list(spec = paste0(e, tr, s), fitted = FALSE, error = conditionMessage(err))
        }
      )
      ets_candidates[[key]] <- result
    }
  }
}
# jsonlite serialises a named list as an object; the diagnosis consumer
# (r_parity_diagnosis.rs) needs an ordered array, so drop the names here.
ets_candidates <- unname(ets_candidates)

diagnosis <- list(
  ets_defaults = ets_defaults,
  ets_candidates = ets_candidates,
  selected = list(
    method = ets_auto_fit$method,
    aicc = as.numeric(ets_auto_fit$aicc)
  )
)

air_fixture <- c(
  provenance("r_forecast", GENERATOR_PATH),
  list(
    tolerance = AIR_TOLERANCE,
    inputs = list(
      name = "AirPassengers",
      frequency = 12,
      start = "1949-01",
      values = as.numeric(air)
    ),
    parameters = REF_PARAMETERS,
    models = air_models,
    diagnosis = diagnosis
  )
)

write_fixture(air_fixture, "r_forecast/air_passengers.json")

# ---- Simulated series family (seeded random walk + intermittent) ---------

rw_ets_fit <- ets(rw)
rw_auto_arima_fit <- auto.arima(rw)
rw_arima010_fit <- Arima(rw, order = c(0, 1, 0))

rw_models <- list(
  naive = fc_block(naive(rw, h = h, level = levels), "naive(rw, h = 12, level = c(80, 95))"),
  rwf_drift = fc_block(
    rwf(rw, h = h, drift = TRUE, level = levels),
    "rwf(rw, h = 12, drift = TRUE, level = c(80, 95))"
  ),
  ses = fc_block(ses(rw, h = h, level = levels), "ses(rw, h = 12, level = c(80, 95))"),
  holt = local({
    f <- holt(rw, h = h, level = levels)
    c(fc_block(f, "holt(rw, h = 12, level = c(80, 95))"), list(fit = ets_fit_block(f$model)))
  }),
  ets_auto = c(
    fc_block(forecast(rw_ets_fit, h = h, level = levels), "forecast(ets(rw), h = 12, level = c(80, 95))"),
    list(fit = ets_fit_block(rw_ets_fit))
  ),
  auto_arima = c(
    fc_block(
      forecast(rw_auto_arima_fit, h = h, level = levels),
      "forecast(auto.arima(rw), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(rw_auto_arima_fit))
  ),
  arima_010 = c(
    fc_block(
      forecast(rw_arima010_fit, h = h, level = levels),
      "forecast(Arima(rw, order = c(0,1,0)), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(rw_arima010_fit))
  ),
  thetaf = fc_block(thetaf(rw, h = h, level = levels), "thetaf(rw, h = 12, level = c(80, 95))")
)

inter_models <- list(
  croston = fc_block(croston(inter, h = h, alpha = 0.1), "croston(inter, h = 12, alpha = 0.1)"),
  naive = fc_block(naive(inter, h = h, level = levels), "naive(inter, h = 12, level = c(80, 95))"),
  ses = local({
    f <- ses(inter, h = h, level = levels)
    c(fc_block(f, "ses(inter, h = 12, level = c(80, 95))"), list(fit = ets_fit_block(f$model)))
  })
)

# ---- trend family (D-08 Task 2: random walk with drift, non-seasonal) ---

trend_ets_fit <- ets(trend)
trend_auto_arima_fit <- auto.arima(trend)

trend_models <- list(
  naive = fc_block(naive(trend, h = h, level = levels), "naive(trend, h = 12, level = c(80, 95))"),
  rwf_drift = fc_block(
    rwf(trend, h = h, drift = TRUE, level = levels),
    "rwf(trend, h = 12, drift = TRUE, level = c(80, 95))"
  ),
  ses = local({
    f <- ses(trend, h = h, level = levels)
    c(fc_block(f, "ses(trend, h = 12, level = c(80, 95))"), list(fit = ets_fit_block(f$model)))
  }),
  holt = local({
    f <- holt(trend, h = h, level = levels)
    c(fc_block(f, "holt(trend, h = 12, level = c(80, 95))"), list(fit = ets_fit_block(f$model)))
  }),
  ets_auto = c(
    fc_block(forecast(trend_ets_fit, h = h, level = levels), "forecast(ets(trend), h = 12, level = c(80, 95))"),
    list(fit = ets_fit_block(trend_ets_fit))
  ),
  auto_arima = c(
    fc_block(
      forecast(trend_auto_arima_fit, h = h, level = levels),
      "forecast(auto.arima(trend), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(trend_auto_arima_fit))
  ),
  thetaf = fc_block(thetaf(trend, h = h, level = levels), "thetaf(trend, h = 12, level = c(80, 95))")
)

# 17-03 Task 3: the 9 rw + 3 inter parity_checks rows. Every tolerance/
# measured value is read from diagnosis/harness-output.txt or the 0.16.1
# measurement recorded in the plan's interfaces block -- never computed here.
RW_PARITY_CHECKS <- list(
  list(
    series = "rw", r_key = "naive", surface = "Naive", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "Naive is closed-form; same z approximation as air/naive.",
    follow_up = "", measured_point = 0, measured_interval = 2.20e-4
  ),
  list(
    series = "rw", r_key = "rwf_drift", surface = "RandomWalkDrift", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-3, status = "parity", cause = "",
    reason = "RandomWalkDrift is closed-form; same drift-interval formula difference as air/rwf_drift.",
    follow_up = "", measured_point = 6.6e-14, measured_interval = 2.30e-3
  ),
  list(
    series = "rw", r_key = "ses", surface = "SESOptimized", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "SimpleExponentialSmoothing::auto() on rw converges to R ses()'s optimum.",
    follow_up = "", measured_point = 5.37e-10, measured_interval = 9.58e-8
  ),
  list(
    series = "rw", r_key = "ses", surface = "ETS:ANN", ets_spec = "ANN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "Same ETS(A,N,N) construction as SESOptimized above.",
    follow_up = "", measured_point = 5.37e-10, measured_interval = 5.89e-8
  ),
  list(
    series = "rw", r_key = "holt", surface = "ETS:AAN", ets_spec = "AAN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 0.01,
    interval_tol = 0.03, status = "known_gap", cause = "RC-02",
    reason = "RC-02: R's own optimiser stops at an inferior, in-bounds optimum (E6: crate_loglik=-515.765392 > r_loglik=-516.366200); measured point=5.60e-3 interval=1.96e-2.",
    follow_up = "known gap: nothing to fix, R itself is the looser optimum.",
    measured_point = 5.60e-3, measured_interval = 1.96e-2
  ),
  list(
    series = "rw", r_key = "holt", surface = "Holt", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 9e-3,
    interval_tol = 0.03, status = "known_gap", cause = "RC-01,RC-02",
    reason = "D-12/D-13 fix (RC-01) remapped SQL 'Holt' to ETS(A,A,N), cutting the deviation 13.41x; the residual (identical to the ETS:AAN row above) is RC-02's R-optimum-is-looser cause, still outside the parity tier. tol = 1.5x post-fix measured (5.60e-3/1.96e-2), capped at the pre-fix planned tolerance (rel 0.1/0.45).",
    follow_up = "fixed: forecast() dispatch and build_forecaster remapped SQL 'Holt' to ETS::new(ETSSpec::aan(), 1) (17-03 Task 1); residual tracked as RC-02.",
    measured_point = 5.60e-3, measured_interval = 1.96e-2
  ),
  list(
    series = "rw", r_key = "ets_auto", surface = "AutoETS", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-5,
    interval_tol = 1e-4, status = "parity", cause = "",
    reason = "AutoETS selects ETS(A,N,N), matching R's ets() selection on this series.",
    follow_up = "", measured_point = 2.92e-7, measured_interval = 3.19e-5
  ),
  list(
    series = "rw", r_key = "auto_arima", surface = "AutoARIMA", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "D-15 tolerance rule on the 0.16.1 measurement (autoarima_report, diagnosis/harness-output.txt): order_match=true (R (0,1,0)), point=0.000000e0, interval=3.276619e-8, both inside the parity tier; tol = max(1e-6, 1.5x measured, rounded up to 1 significant digit) -- the 1e-6 floor binds on both.",
    follow_up = "", measured_point = 0, measured_interval = 3.276619e-8
  ),
  list(
    series = "rw", r_key = "thetaf", surface = "Theta", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "Theta matches R thetaf() to numerical precision on rw.",
    follow_up = "", measured_point = 3.76e-10, measured_interval = 5.77e-8
  )
)
INTER_PARITY_CHECKS <- list(
  list(
    series = "inter", r_key = "croston", surface = "CrostonClassic", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = NA_real_, status = "parity", cause = "",
    reason = "CrostonClassic is closed-form (alpha 0.1); R's croston() has no interval.",
    follow_up = "", measured_point = 2.2e-15, measured_interval = NA_real_
  ),
  list(
    series = "inter", r_key = "naive", surface = "Naive", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "Naive is closed-form; same z approximation as air/naive.",
    follow_up = "", measured_point = 0, measured_interval = 2.20e-4
  ),
  list(
    series = "inter", r_key = "ses", surface = "SESOptimized", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1.0,
    interval_tol = 0.5, status = "known_gap", cause = "RC-04",
    reason = "RC-04: on this zero-inflated series the crate's own optimiser converges to a different (alpha, l0) optimum than R's ses() -- evaluated at R's parameters, the crate's own objective scores R's point strictly better than its own converged fit; measured point=7.39e-1 interval=3.83e-1.",
    follow_up = "draft: RC-04-ses-optimized-intermittent-optimum.md (pending user filing).",
    measured_point = 7.39e-1, measured_interval = 3.83e-1
  )
)

# D-08/D-09/D-12 (17-03 Task 2): the 9 trend parity_checks rows. Every
# tolerance/measured value here is read from diagnosis/harness-output.txt
# (r_parity_diagnosis.rs's diagnosis_report / autoarima_report), never
# computed in R.
TREND_PARITY_CHECKS <- list(
  list(
    series = "trend", r_key = "naive", surface = "Naive", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-4, status = "parity", cause = "",
    reason = "Naive is closed-form; bounds use the crate's Abramowitz-Stegun 26.2.23 normal-quantile approximation (constant ~2.2e-4 relative half-width error on every series).",
    follow_up = "", measured_point = 0, measured_interval = 2.20e-4
  ),
  list(
    series = "trend", r_key = "rwf_drift", surface = "RandomWalkDrift", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "abs", point_tol = 1e-8,
    interval_tol = 5e-3, status = "parity", cause = "",
    reason = "RandomWalkDrift is closed-form; bounds combine the z approximation with the crate's drift-interval formula, which differs slightly from R's rwf().",
    follow_up = "", measured_point = 7.1e-14, measured_interval = 3.14e-3
  ),
  list(
    series = "trend", r_key = "ses", surface = "SESOptimized", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "SimpleExponentialSmoothing::auto() on a non-intermittent series converges to R ses()'s optimum (contrast inter/ses, RC-04).",
    follow_up = "", measured_point = 1.96e-10, measured_interval = 5.95e-8
  ),
  list(
    series = "trend", r_key = "ses", surface = "ETS:ANN", ets_spec = "ANN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "Same ETS(A,N,N) construction as SESOptimized above.",
    follow_up = "", measured_point = 1.96e-10, measured_interval = 1.87e-8
  ),
  list(
    series = "trend", r_key = "holt", surface = "ETS:AAN", ets_spec = "AAN",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 2e-3,
    interval_tol = 1e-2, status = "parity", cause = "",
    reason = "ETS(A,A,N) direct construction, R's own holt() definition; measured by diagnosis_report's trend/holt/Holt E4 block (the 'ETS(A,A,N) R-faithful' line).",
    follow_up = "", measured_point = 5.495e-4, measured_interval = 6.532e-3
  ),
  list(
    series = "trend", r_key = "holt", surface = "Holt", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 2e-3,
    interval_tol = 1e-2, status = "parity", cause = "RC-01",
    reason = "D-12/D-13 fix (RC-01): SQL 'Holt' now dispatches to ETS(A,A,N) (R's holt() definition) instead of HoltLinearTrend::auto(), so this check is identical to the ETS:AAN row above and is in the parity tier post-fix.",
    follow_up = "fixed: forecast() dispatch and build_forecaster remapped SQL 'Holt' to ETS::new(ETSSpec::aan(), 1) (17-03 Task 1).",
    measured_point = 5.495e-4, measured_interval = 6.532e-3
  ),
  list(
    series = "trend", r_key = "ets_auto", surface = "AutoETS", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 2e-3,
    interval_tol = 1e-2, status = "parity", cause = "",
    reason = "AutoETS selects ETS(A,A,N), matching R's ets() selection on this series (contrast air/ets_auto, RC-03, where R and the crate's selection diverge).",
    follow_up = "", measured_point = 5.63e-4, measured_interval = 6.69e-3
  ),
  list(
    series = "trend", r_key = "auto_arima", surface = "AutoARIMA", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 6e-3, status = "parity", cause = "",
    reason = "D-15 tolerance rule on the 0.16.1 measurement (autoarima_report, diagnosis/harness-output.txt): order_match=true (R (0,1,0) with drift), point=2.232548e-14, interval=3.361326e-3, both inside the parity tier; tol = max(1e-6, 1.5x measured, rounded up to 1 significant digit).",
    follow_up = "", measured_point = 2.232548e-14, measured_interval = 3.361326e-3
  ),
  list(
    series = "trend", r_key = "thetaf", surface = "Theta", ets_spec = "",
    seasonal_period = 0, point_tol_kind = "rel", point_tol = 1e-6,
    interval_tol = 1e-6, status = "parity", cause = "",
    reason = "Theta matches R thetaf() to numerical precision on this non-seasonal series.",
    follow_up = "", measured_point = 2.79e-10, measured_interval = 1.86e-8
  )
)
SIM_TOLERANCE <- c(REF_TOLERANCE, list(parity_checks = c(RW_PARITY_CHECKS, INTER_PARITY_CHECKS, TREND_PARITY_CHECKS)))

# ---- ar1_l10 family (D-09: non-seasonal arima_order parity, d=0) ---------

ar1_l10_arima100_fit <- Arima(ar1_l10, order = c(1, 0, 0))
ar1_l10_arima101_fit <- Arima(ar1_l10, order = c(1, 0, 1))

ar1_l10_models <- list(
  arima_100 = c(
    fc_block(
      forecast(ar1_l10_arima100_fit, h = h, level = levels),
      "forecast(Arima(ar1_l10, order = c(1,0,0)), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(ar1_l10_arima100_fit))
  ),
  arima_101 = c(
    fc_block(
      forecast(ar1_l10_arima101_fit, h = h, level = levels),
      "forecast(Arima(ar1_l10, order = c(1,0,1)), h = 12, level = c(80, 95))"
    ),
    list(fit = arima_fit_block(ar1_l10_arima101_fit))
  )
)

# ---- auto.arima selection-frequency block (UPST-01, D-15) ---------------
#
# Appended AFTER every other RNG-consuming call in this script, in its own
# local() with its own explicit seed, so no earlier fixture value depends on
# this draw. auto.arima()/ets() are deterministic optimisers (no RNG use),
# so reseeding here cannot change anything computed above.
auto_arima_selection <- local({
  set.seed(170015)
  n <- 100
  one_series <- function(y) {
    fit <- auto.arima(y)
    ord <- arimaorder(fit)
    list(
      values = as.numeric(y),
      order = list(p = as.numeric(ord["p"]), d = as.numeric(ord["d"]), q = as.numeric(ord["q"])),
      constant = any(c("drift", "intercept") %in% names(coef(fit)))
    )
  }
  rw_series <- lapply(1:30, function(i) one_series(cumsum(rnorm(n))))
  ar07_series <- lapply(1:30, function(i) one_series(as.numeric(arima.sim(list(ar = 0.7), n = n))))
  list(seed = 170015, n = n, rw = rw_series, ar07 = ar07_series)
})

sim_fixture <- c(
  provenance("r_forecast", GENERATOR_PATH),
  list(
    tolerance = SIM_TOLERANCE,
    parameters = REF_PARAMETERS,
    series = list(
      rw = list(
        inputs = list(
          values = as.numeric(rw),
          frequency = 1,
          seed_note = "set.seed(20261008); rw = cumsum(rnorm(200)) — see common.R"
        ),
        models = rw_models
      ),
      inter = list(
        inputs = list(
          values = as.numeric(inter),
          frequency = 1,
          seed_note = "set.seed(20261008); inter = ifelse(runif(120) < 0.3, rpois(120, 3) + 1, 0) — see common.R"
        ),
        models = inter_models
      ),
      ar1_l10 = list(
        inputs = list(
          values = as.numeric(ar1_l10),
          frequency = 1,
          seed_note = "10 + arima.sim(list(ar = 0.7), n = 300) from common.R reference_series()"
        ),
        models = ar1_l10_models
      ),
      trend = list(
        inputs = list(
          values = as.numeric(trend),
          frequency = 1,
          seed_note = paste(
            "trend <- cumsum(rnorm(150, mean = 0.3, sd = 1)), appended LAST in",
            "common.R reference_series() (D-08 Task 2) -- random walk with",
            "drift, true model rwf(drift) / ARIMA(0,1,0) with drift"
          )
        ),
        models = trend_models
      )
    ),
    auto_arima_selection = auto_arima_selection
  )
)

write_fixture(sim_fixture, "r_forecast/simulated_series.json")
