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

REF_TOLERANCE <- list(
  closed_form_abs = 1e-8,
  model_point_rel = 0.01,
  interval_rel = 0.05,
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
  holt = fc_block(holt(air, h = h, level = levels), "holt(air, h = 12, level = c(80, 95))"),
  hw_additive = fc_block(
    hw(air, h = h, level = levels, seasonal = "additive"),
    "hw(air, h = 12, level = c(80, 95), seasonal = \"additive\")"
  ),
  hw_multiplicative = fc_block(
    hw(air, h = h, level = levels, seasonal = "multiplicative"),
    "hw(air, h = 12, level = c(80, 95), seasonal = \"multiplicative\")"
  ),
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
    models = air_models
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
  holt = fc_block(holt(rw, h = h, level = levels), "holt(rw, h = 12, level = c(80, 95))"),
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
  ses = fc_block(ses(inter, h = h, level = levels), "ses(inter, h = 12, level = c(80, 95))")
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
