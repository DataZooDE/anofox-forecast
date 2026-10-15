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
    checks = list("air/holt/Holt", "rw/holt/Holt"),
    location = "extension",
    cause = paste(
      "Wrapper's SQL 'Holt' dispatched to HoltLinearTrend::auto() (SSE",
      "objective, trend seeded from a single first difference) instead of",
      "ETS(A,A,N), R holt()'s actual definition; the R-faithful ETS(A,A,N)",
      "construction cuts the point deviation 6.86x on AirPassengers and",
      "13.41x on rw."
    ),
    evidence = paste(
      "E4 mapping check (diagnosis/harness-output.txt): air",
      "HoltLinearTrend point_dev=1.155e-1 interval_dev=2.312e-1 ->",
      "ETS(A,A,N) R-faithful point_dev=1.684e-2 interval_dev=3.334e-2",
      "(cut 6.86x); rw HoltLinearTrend point_dev=7.517e-2",
      "interval_dev=3.262e-1 -> ETS(A,A,N) R-faithful point_dev=5.604e-3",
      "interval_dev=1.960e-2 (cut 13.41x)."
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
  notes = paste(
    "closed_form_abs is exact (naive/snaive/rwf drift are closed-form);",
    "model_point_rel and interval_rel are parity targets for later",
    "phases (Phase 10 FCST-01, Phase 17 FCST-09), not asserted in Phase 9."
  )
)
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
    tolerance = REF_TOLERANCE,
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

sim_fixture <- c(
  provenance("r_forecast", GENERATOR_PATH),
  list(
    tolerance = REF_TOLERANCE,
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
      )
    )
  )
)

write_fixture(sim_fixture, "r_forecast/simulated_series.json")
