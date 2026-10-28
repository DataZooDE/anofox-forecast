# R diagnostics reference generator (TEST-01).
#
# Writes validation/reference/fixtures/r_diagnostics/stationarity_portmanteau.json
# — ADF (tseries + urca)/KPSS/Ljung-Box/Box-Pierce/Jarque-Bera reference
# statistics consumed by Phases 11 and 17 (DIAG-01/02/04).
#
# Regenerate with:
#   Rscript validation/reference/r/gen_diagnostics_reference.R
# (works from any cwd — the script resolves its own directory via --file=).

# common.R is not loaded yet, so the script-dir resolution is inlined here.
.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages({
  library(tseries)
  library(urca)
})

GENERATOR_PATH <- "validation/reference/r/gen_diagnostics_reference.R"

series <- reference_series()

# ---- urca ur.df result extractors ----------------------------------------

urdf_block <- function(u) {
  teststat_vec <- u@teststat[1, ]
  cval_mat <- u@cval
  cval_list <- lapply(seq_len(nrow(cval_mat)), function(i) as.list(cval_mat[i, ]))
  names(cval_list) <- rownames(cval_mat)
  list(
    teststat = as.list(teststat_vec),
    cval = cval_list
  )
}

urdf_aic_block <- function(u) {
  block <- urdf_block(u)
  coef_names <- rownames(u@testreg$coefficients)
  block$selected_lags <- sum(grepl("^z\\.diff\\.lag", coef_names))
  block
}

# ---- per-series diagnostic battery ----------------------------------------

diag_tests <- function(y) {
  n <- length(y)
  k_fixed <- trunc((n - 1)^(1 / 3))

  adf <- tseries::adf.test(y, k = k_fixed)
  urdf_drift_lag1 <- ur.df(y, type = "drift", lags = 1)
  urdf_drift_lag4 <- ur.df(y, type = "drift", lags = 4)
  urdf_trend_lag1 <- ur.df(y, type = "trend", lags = 1)
  urdf_drift_aic <- ur.df(y, type = "drift", lags = k_fixed, selectlags = "AIC")
  urdf_trend_aic <- ur.df(y, type = "trend", lags = k_fixed, selectlags = "AIC")
  kpss_level <- suppressWarnings(tseries::kpss.test(y, null = "Level", lshort = TRUE))
  kpss_trend <- suppressWarnings(tseries::kpss.test(y, null = "Trend", lshort = TRUE))
  lb_fitdf0 <- Box.test(y, lag = 10, type = "Ljung-Box", fitdf = 0)
  lb_fitdf2 <- Box.test(y, lag = 10, type = "Ljung-Box", fitdf = 2)
  bp <- Box.test(y, lag = 10, type = "Box-Pierce")
  jb <- tseries::jarque.bera.test(y)

  list(
    adf_tseries = list(
      statistic = as.numeric(adf$statistic),
      lag_order = as.numeric(adf$parameter),
      p_value = as.numeric(adf$p.value)
    ),
    urdf_drift_lag1 = urdf_block(urdf_drift_lag1),
    urdf_drift_lag4 = urdf_block(urdf_drift_lag4),
    urdf_trend_lag1 = urdf_block(urdf_trend_lag1),
    urdf_drift_aic = urdf_aic_block(urdf_drift_aic),
    urdf_trend_aic = urdf_aic_block(urdf_trend_aic),
    kpss_level = list(
      statistic = as.numeric(kpss_level$statistic),
      lag = as.numeric(kpss_level$parameter),
      p_value = as.numeric(kpss_level$p.value)
    ),
    kpss_trend = list(
      statistic = as.numeric(kpss_trend$statistic),
      lag = as.numeric(kpss_trend$parameter),
      p_value = as.numeric(kpss_trend$p.value)
    ),
    ljung_box_lag10_fitdf0 = list(
      statistic = as.numeric(lb_fitdf0$statistic),
      df = as.numeric(lb_fitdf0$parameter),
      p_value = as.numeric(lb_fitdf0$p.value)
    ),
    ljung_box_lag10_fitdf2 = list(
      statistic = as.numeric(lb_fitdf2$statistic),
      df = as.numeric(lb_fitdf2$parameter),
      p_value = as.numeric(lb_fitdf2$p.value)
    ),
    box_pierce_lag10 = list(
      statistic = as.numeric(bp$statistic),
      df = as.numeric(bp$parameter),
      p_value = as.numeric(bp$p.value)
    ),
    jarque_bera = list(
      statistic = as.numeric(jb$statistic),
      p_value = as.numeric(jb$p.value)
    )
  )
}

series_block <- function(y) {
  list(values = as.numeric(y), n = length(y), tests = diag_tests(y))
}

# ---- 17-02: Ljung-Box fitdf / Durbin-Watson portmanteau cases -------------
#
# Model-aware Ljung-Box (D-06, DIAG-04): R Box.test(fitdf = p + q) on the
# residuals of a fitted ARMA(1,1) model, and on first-differenced random-walk
# series (fitdf 2 to exercise a non-zero correction even without a real
# model). durbin_watson is the closed form sum(diff(values)^2)/sum(values^2)
# -- no separate R call needed since this matches the extension's own
# formula exactly. durbin_watson_p_value (#284) is the two-sided normal
# approximation with the exact null moments of lmtest::dwtest(exact = FALSE)
# at a design with no columns (residuals taken as given; dwtest itself
# rejects a zero-column design): E[d] = 2(n-1)/n,
# Var[d] = 2((6n-8) - 2(n-1) E[d]) / (n(n+2)).

arma11_fit <- arima(series$ar1[1:120], order = c(1, 0, 1))
arma11_resid <- as.numeric(residuals(arma11_fit))
diff_rw_full <- diff(series$rw)
diff_rw30 <- diff_rw_full[1:30]

dw_p_value <- function(values) {
  n <- length(values)
  d <- sum(diff(values)^2) / sum(values^2)
  P <- 2 * (n - 1)
  Q <- 6 * n - 8
  m <- P / n
  v <- 2 * (Q - P * m) / (n * (n + 2))
  pz <- pnorm((d - m) / sqrt(v))
  2 * min(pz, 1 - pz)
}

portmanteau_case <- function(name, r_call, values, lag, fitdf) {
  bt <- Box.test(values, lag = lag, type = "Ljung-Box", fitdf = fitdf)
  list(
    name = name,
    r_call = r_call,
    values = as.numeric(values),
    lag = lag,
    fitdf = fitdf,
    statistic = as.numeric(bt$statistic),
    df = as.numeric(bt$parameter),
    p_value = as.numeric(bt$p.value),
    durbin_watson = sum(diff(values)^2) / sum(values^2),
    durbin_watson_p_value = dw_p_value(values)
  )
}

portmanteau_cases <- list(
  arma11_resid_lag10_fitdf2 = portmanteau_case(
    "arma11_resid_lag10_fitdf2",
    "Box.test(residuals(arima(ar1[1:120], order=c(1,0,1))), lag=10, fitdf=2)",
    arma11_resid, 10, 2
  ),
  arma11_resid_lag20_fitdf2 = portmanteau_case(
    "arma11_resid_lag20_fitdf2",
    "Box.test(residuals(arima(ar1[1:120], order=c(1,0,1))), lag=20, fitdf=2)",
    arma11_resid, 20, 2
  ),
  diff_rw_lag40_fitdf2 = portmanteau_case(
    "diff_rw_lag40_fitdf2",
    "Box.test(diff(rw), lag=40, fitdf=2)",
    diff_rw_full, 40, 2
  ),
  diff_rw30_lag6_fitdf2 = portmanteau_case(
    "diff_rw30_lag6_fitdf2",
    "Box.test(diff(rw)[1:30], lag=6, fitdf=2)",
    diff_rw30, 6, 2
  )
)

diag_fixture <- c(
  provenance("r_diagnostics", GENERATOR_PATH),
  list(
    tolerance = list(
      statistic_abs = 1e-6,
      p_value_abs = 1e-4,
      notes = paste(
        "p-values come from table interpolation / response-surface",
        "regressions in tseries/urca, not closed-form; consumer phases may",
        "tighten this tolerance with documented rationale."
      ),
      portmanteau_statistic_abs = 1e-9,
      portmanteau_p_value_abs = 1e-9,
      dw_abs = 1e-12,
      dw_p_value_abs = 1e-9
    ),
    series = list(
      air = series_block(series$air),
      log_air = series_block(log(series$air)),
      rw = series_block(series$rw),
      diff_rw = series_block(diff(series$rw)),
      ar1 = series_block(series$ar1)
    ),
    portmanteau_cases = portmanteau_cases
  )
)

write_fixture(diag_fixture, "r_diagnostics/stationarity_portmanteau.json")
