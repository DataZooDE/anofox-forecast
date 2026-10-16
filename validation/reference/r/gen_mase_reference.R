# R Hyndman-Koehler MASE reference generator (Phase 13 plan 13-04, D-08, STAT-06).
#
# Writes validation/reference/fixtures/r_mase/accuracy_mase.json: for each case,
# the in-sample (training) series, the evaluation actual/forecast arrays, the
# seasonal_period (lag m), and TWO independently-computed expected MASE values
# that must agree with each other (both recorded so a mismatch between them is
# itself a signal the case or the formula is wrong):
#   - mase_accuracy: forecast::accuracy(f, x)'s own MASE column
#   - mase_manual:   mean(|actual-forecast|, na.rm) / mean(|diff(insample, lag=m)|, na.rm)
#
# Consumed by crates/anofox-fcst-core/tests/metrics_reference.rs to pin
# anofox_fcst_core::metrics::mase (Hyndman & Koehler 2006 MASE, scaled by the
# in-sample seasonal-naive MAE).
#
# Regenerate with:
#   Rscript validation/reference/r/gen_mase_reference.R

.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages({
  library(forecast)
})

GENERATOR_PATH <- "validation/reference/r/gen_mase_reference.R"

s <- reference_series()

# mean(|diff(insample, lag=m)|, na.rm=TRUE) / mean(|actual-forecast|, na.rm=TRUE)
# -- same na.rm=TRUE semantics forecast::accuracy() uses internally, which is
# also exactly the Hyndman-Koehler "skip lag pairs / evaluation pairs touching
# a missing value" rule (D-08): no splicing, positions preserved.
mase_manual <- function(actual, forecast, insample, m) {
  mae <- mean(abs(actual - forecast), na.rm = TRUE)
  scale <- mean(abs(diff(insample, lag = m)), na.rm = TRUE)
  mae / scale
}

accuracy_mase <- function(fc_obj, test) {
  acc <- accuracy(fc_obj, test)
  as.numeric(acc["Test set", "MASE"])
}

# --- air_snaive_m12: AirPassengers, snaive(train, 12) ---
air_train <- window(AirPassengers, end = c(1959, 12))
air_test <- window(AirPassengers, start = c(1960, 1))
air_fc <- snaive(air_train, h = length(air_test))
case_air_snaive_m12 <- list(
  insample = as.numeric(air_train),
  actual = as.numeric(air_test),
  forecast = as.numeric(air_fc$mean),
  seasonal_period = as.integer(frequency(air_train)),
  mase_accuracy = accuracy_mase(air_fc, air_test),
  mase_manual = mase_manual(as.numeric(air_test), as.numeric(air_fc$mean), as.numeric(air_train), frequency(air_train)),
  r_call = "accuracy(snaive(window(AirPassengers, end=c(1959,12)), h=12), window(AirPassengers, start=c(1960,1)))"
)

# --- air_ets_m12: AirPassengers, ets(train) forecast mean ---
air_ets_fc <- forecast(ets(air_train), h = length(air_test))
case_air_ets_m12 <- list(
  insample = as.numeric(air_train),
  actual = as.numeric(air_test),
  forecast = as.numeric(air_ets_fc$mean),
  seasonal_period = as.integer(frequency(air_train)),
  mase_accuracy = accuracy_mase(air_ets_fc, air_test),
  mase_manual = mase_manual(as.numeric(air_test), as.numeric(air_ets_fc$mean), as.numeric(air_train), frequency(air_train)),
  r_call = "accuracy(forecast(ets(window(AirPassengers, end=c(1959,12))), h=12), window(AirPassengers, start=c(1960,1)))"
)

# --- rw_rwf_m1: reference_series()$rw[1:180] train / [181:200] test, rwf(drift=TRUE), frequency 1 ---
rw <- s$rw
rw_train <- rw[1:180]
rw_test <- rw[181:200]
rw_train_ts <- ts(rw_train, frequency = 1)
rw_fc <- rwf(rw_train_ts, h = length(rw_test), drift = TRUE)
case_rw_rwf_m1 <- list(
  insample = as.numeric(rw_train),
  actual = as.numeric(rw_test),
  forecast = as.numeric(rw_fc$mean),
  seasonal_period = 1L,
  mase_accuracy = accuracy_mase(rw_fc, rw_test),
  mase_manual = mase_manual(rw_test, as.numeric(rw_fc$mean), rw_train, 1),
  r_call = "accuracy(rwf(ts(reference_series()$rw[1:180], frequency=1), h=20, drift=TRUE), reference_series()$rw[181:200])"
)

# --- deterministic formula shared by det_m4/det_m1/insample_na_m4/test_na_m4 ---
det_y <- function(t) 10 + 2 * sin(2 * pi * t / 4) + 0.1 * t + cos(1.9 * t)
det_fc_formula <- function(t) 10 + 2 * sin(2 * pi * t / 4) + 0.1 * t

det_t_train <- 1:40
det_t_test <- 41:48
det_train_vals <- det_y(det_t_train)
det_test_vals <- det_y(det_t_test)
det_fc_vals <- det_fc_formula(det_t_test)

# `fitted` must be supplied explicitly (NA-filled, same length/time attributes
# as `x`): forecast::accuracy.forecast treats any list as a "training set"
# too (`trainset <- is.list(object)`) and unconditionally calls
# `fitted(object)` to compute training residuals; with no `fitted`/
# `fitted.values` field, `fitted()` returns NULL and `dx - NULL` collapses to
# a length-0 vector, which errors assigning tsp() in Ops.ts. The resulting
# "Training set" row is NA/discarded -- only the "Test set" row (computed by
# `testaccuracy`, independent of `fitted`) is read via `accuracy_mase()`.
make_manual_forecast <- function(train_vals, fc_vals, frequency, fc_start) {
  train_ts <- ts(train_vals, frequency = frequency)
  structure(
    list(
      mean = ts(fc_vals, start = fc_start, frequency = frequency),
      x = train_ts,
      fitted = ts(rep(NA_real_, length(train_vals)), start = start(train_ts), frequency = frequency),
      method = "manual"
    ),
    class = "forecast"
  )
}

# accuracy.forecast's Test-set MASE scale uses `D <- as.numeric(frequency(x) >
# 1)` / `d <- as.numeric(frequency(x) == 1)` from the TEST argument `x`'s OWN
# frequency attribute (not the training frequency) to decide seasonal
# (lag = training frequency) vs non-seasonal (lag 1) differencing -- passing
# a plain numeric vector (frequency 1 by default) silently forces lag-1 even
# for a seasonal case. For det_m4/insample_na_m4/test_na_m4 (seasonal_period
# 4) the test vector must be wrapped as a frequency-4 ts so D=1 triggers the
# lag-4 branch; verified directly (lag-1 would give 0.3292 instead of the
# lag-4 0.8156 both this script and mase_manual() converge on).
det_test_ts <- ts(det_test_vals, start = c(11, 1), frequency = 4)

# --- det_m4: frequency 4, lag 4 ---
det_m4_fc <- make_manual_forecast(det_train_vals, det_fc_vals, 4, c(11, 1))
case_det_m4 <- list(
  insample = as.numeric(det_train_vals),
  actual = as.numeric(det_test_vals),
  forecast = as.numeric(det_fc_vals),
  seasonal_period = 4L,
  mase_accuracy = accuracy_mase(det_m4_fc, det_test_ts),
  mase_manual = mase_manual(det_test_vals, det_fc_vals, det_train_vals, 4),
  r_call = "accuracy(manual forecast object (y=10+2*sin(2*pi*t/4)+0.1*t+cos(1.9*t), fc=10+2*sin(2*pi*t/4)+0.1*t, t=1:40/41:48, frequency=4), ts(det_test_vals, start=c(11,1), frequency=4))"
)

# --- det_m1: same formula, frequency 1, lag 1 ---
det_m1_fc <- make_manual_forecast(det_train_vals, det_fc_vals, 1, length(det_train_vals) + 1)
case_det_m1 <- list(
  insample = as.numeric(det_train_vals),
  actual = as.numeric(det_test_vals),
  forecast = as.numeric(det_fc_vals),
  seasonal_period = 1L,
  mase_accuracy = accuracy_mase(det_m1_fc, det_test_vals),
  mase_manual = mase_manual(det_test_vals, det_fc_vals, det_train_vals, 1),
  r_call = "accuracy(manual forecast object (same det formula, frequency=1), det_test_vals)"
)

# --- insample_na_m4: det_m4 train with an NA at t=10, manual forecast object ---
det_train_na <- det_train_vals
det_train_na[10] <- NA_real_
insample_na_m4_fc <- make_manual_forecast(det_train_na, det_fc_vals, 4, c(11, 1))
case_insample_na_m4 <- list(
  insample = as.numeric(det_train_na),
  actual = as.numeric(det_test_vals),
  forecast = as.numeric(det_fc_vals),
  seasonal_period = 4L,
  mase_accuracy = accuracy_mase(insample_na_m4_fc, det_test_ts),
  mase_manual = mase_manual(det_test_vals, det_fc_vals, det_train_na, 4),
  r_call = "accuracy(manual forecast object, ts(det_test_vals,...,frequency=4)); insample[10] <- NA (det_m4 train with one NA)"
)

# --- test_na_m4: det_m4 with an NA in the test actuals ---
det_test_na <- det_test_vals
det_test_na[3] <- NA_real_
det_test_na_ts <- ts(det_test_na, start = c(11, 1), frequency = 4)
case_test_na_m4 <- list(
  insample = as.numeric(det_train_vals),
  actual = as.numeric(det_test_na),
  forecast = as.numeric(det_fc_vals),
  seasonal_period = 4L,
  mase_accuracy = accuracy_mase(det_m4_fc, det_test_na_ts),
  mase_manual = mase_manual(det_test_na, det_fc_vals, det_train_vals, 4),
  r_call = "accuracy(det_m4's manual forecast object, ts(det_test_na,...,frequency=4)); actual[3] <- NA"
)

# --- constant_insample: train rep(3, 20), frequency 1 -> R returns Inf (scale=0) ---
const_train <- rep(3, 20)
const_test <- c(5, 6)
const_fc_vals <- c(4, 5)
const_fc <- make_manual_forecast(const_train, const_fc_vals, 1, length(const_train) + 1)
case_constant_insample <- list(
  insample = as.numeric(const_train),
  actual = as.numeric(const_test),
  forecast = as.numeric(const_fc_vals),
  seasonal_period = 1L,
  mase_accuracy = accuracy_mase(const_fc, const_test),
  mase_manual = mase_manual(const_test, const_fc_vals, const_train, 1),
  r_call = "accuracy(manual forecast object, const_test); insample is constant rep(3,20) -> scale 0 -> R returns Inf. Product policy (D-08): MASE is undefined here, returned as SQL NULL, not Inf."
)

tv <- provenance("r_mase", GENERATOR_PATH)$tool_versions
tv$forecast <- as.character(packageVersion("forecast"))

mase_fixture <- list(
  family = "r_mase",
  generator = GENERATOR_PATH,
  tool_versions = tv,
  tolerance = list(
    mase_abs = 1e-12,
    notes = "same formula (MAE / mean(|diff(insample,lag=m)|)); mase_accuracy and mase_manual must agree to this tolerance for every case (constant_insample -> both Inf)"
  ),
  cases = list(
    air_snaive_m12 = case_air_snaive_m12,
    air_ets_m12 = case_air_ets_m12,
    rw_rwf_m1 = case_rw_rwf_m1,
    det_m4 = case_det_m4,
    det_m1 = case_det_m1,
    insample_na_m4 = case_insample_na_m4,
    test_na_m4 = case_test_na_m4,
    constant_insample = case_constant_insample
  )
)

write_fixture(mase_fixture, "r_mase/accuracy_mase.json")
