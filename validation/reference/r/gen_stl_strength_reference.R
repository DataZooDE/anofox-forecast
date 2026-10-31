# R STL / trend+seasonal-strength reference generator (Phase 13 plan 13-02,
# STAT-02, D-02).
#
# Writes validation/reference/fixtures/r_stl_strength/cases.json — per-case
# input series + period(s), the stats::stl() components (single-period
# cases), the forecast::mstl() columns (multi-period case), the
# stats::supsmu() trend (non-seasonal/short cases), and the
# tsfeatures::stl_features() trend/seasonal-strength outputs — consumed by
# crates/anofox-fcst-core/tests/stl_reference.rs to pin stl.rs/supsmu.rs/
# stats::stl_strengths against R.
#
# feasts is NOT installed (D-02 names it, but the repo's R environment only
# has tsfeatures — see validation/reference/README.md section 5); tsfeatures
# is Hyndman's own implementation of the same F_T/F_S features, built on
# forecast::mstl -> stats::stl, so it is the reference used here.
#
# Regenerate with:
#   Rscript validation/reference/r/gen_stl_strength_reference.R
# (works from any cwd — the script resolves its own directory via --file=).

.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages({
  library(forecast)
  library(tsfeatures)
})

GENERATOR_PATH <- "validation/reference/r/gen_stl_strength_reference.R"

s <- reference_series()

# --- case input series --------------------------------------------------

audit_rw150_values <- c(
  -1.454, -1.6625, -2.2946, -4.0556, -3.3207, -3.3441, -3.2727, -4.025, -3.5702, -4.1095,
  -4.2524, -5.3607, -6.5768, -5.2413, -5.7484, -5.4567, -5.4905, -5.9316, -6.4396, -5.8095,
  -6.1114, -6.2628, -6.2406, -5.0641, -4.3836, -4.001, -4.5645, -5.9465, -4.997, -4.0305,
  -4.1712, -3.6293, -2.8479, -2.0167, -1.0953, -1.551, -0.036, -1.2826, -0.4209, 0.0731,
  0.9467, 2.8257, 4.3102, 3.165, 1.4763, 2.2932, 1.2782, 1.2658, 2.1055, 0.4617,
  -1.6483, -1.389, -1.3446, -1.5904, -1.5519, -2.4124, -3.9259, -4.0925, -5.0642, -6.7077,
  -6.202, -6.2634, -5.8569, -6.8462, -7.5043, -8.5033, -9.3899, -9.1945, -9.9775, -9.6214,
  -9.2817, -7.2565, -8.6493, -7.7614, -7.8509, -7.8649, -9.3148, -9.775, -9.0318, -9.1143,
  -9.0332, -9.3239, -8.1694, -8.1908, -10.3912, -11.0833, -13.0521, -16.3036, -16.8337, -15.5001,
  -15.453, -16.6255, -17.5662, -16.4356, -16.278, -16.23, -16.2835, -16.2451, -15.4397, -14.8871,
  -14.6714, -15.7143, -15.2031, -15.8874, -14.7935, -16.0646, -16.2022, -16.2096, -17.5342, -15.8122,
  -14.3518, -14.8154, -14.0437, -13.665, -16.2786, -16.0282, -16.0895, -16.0063, -17.0832, -17.3525,
  -17.5308, -16.3427, -16.0083, -16.0138, -14.4849, -15.0401, -15.4295, -17.2463, -15.6772, -14.7129,
  -13.796, -13.1271, -13.017, -12.8015, -13.0535, -13.2571, -13.2028, -11.6909, -11.1353, -11.1937,
  -11.7731, -12.4081, -10.8054, -10.2987, -10.2312, -10.5773, -11.6864, -11.7533, -10.8796, -11.2721
)

air_values <- as.numeric(s$air)
ref_rw200_values <- as.numeric(s$rw)

det_seasonal84_t <- 1:84
det_seasonal84_values <- 10 + 3 * sin(2 * pi * det_seasonal84_t / 7) + 0.1 * det_seasonal84_t +
  0.5 * cos(2.1 * det_seasonal84_t)

# #286 (BUG-04) repro series: n=84, weekly (period 7) sine + trend + two
# off-period nuisance terms (i is 0-indexed in the issue's SQL; t = i here).
issue286_weekly84_t <- 0:83
issue286_weekly84_values <- 20 + 0.3 * issue286_weekly84_t +
  5 * sin(2 * pi * issue286_weekly84_t / 7) +
  1.6 * cos(issue286_weekly84_t * 0.9) +
  0.7 * sin(issue286_weekly84_t * 1.7 + 20)

msts_t <- 1:280
msts_values <- sin(2 * pi * msts_t / 7) + 0.5 * sin(2 * pi * msts_t / 28) + 0.3 * cos(1.7 * msts_t)

det_trend60_t <- 1:60
det_trend60_values <- 0.5 * det_trend60_t + 2 * sin(1.3 * det_trend60_t)

set.seed(20261011)
wn100_values <- rnorm(100)

short_seasonal14_values <- det_seasonal84_values[1:14]

constant_48_values <- rep(5, 48)

# --- helpers --------------------------------------------------------------

# Single-period tsfeatures::stl_features + stats::stl() components.
single_period_case <- function(values, period) {
  x <- ts(values, frequency = period)
  fit <- stl(x, s.window = 11)
  ts_feat <- tsfeatures::stl_features(x)

  list(
    values = as.numeric(values),
    periods = list(period),
    stl = list(
      s_window = 11,
      win = as.list(setNames(as.numeric(fit$win), names(fit$win))),
      jump = as.list(setNames(as.numeric(fit$jump), names(fit$jump))),
      deg = as.list(setNames(as.numeric(fit$deg), names(fit$deg))),
      seasonal = as.numeric(fit$time.series[, "seasonal"]),
      trend = as.numeric(fit$time.series[, "trend"]),
      remainder = as.numeric(fit$time.series[, "remainder"])
    ),
    tsfeatures = list(
      trend = unname_or_na(ts_feat["trend"]),
      seasonal_strength = unname_or_na(ts_feat["seasonal_strength"])
    )
  )
}

# Multi-period (msts) case: forecast::mstl() columns + tsfeatures per-period
# seasonal strengths.
multi_period_case <- function(values, periods) {
  x <- msts(values, seasonal.periods = periods)
  fit <- forecast::mstl(x)
  ts_feat <- tsfeatures::stl_features(x)

  seasonal_cols <- grep("^Seasonal", colnames(fit))
  seasonals <- lapply(seasonal_cols, function(j) as.numeric(fit[, j]))
  names(seasonals) <- colnames(fit)[seasonal_cols]

  seasonal_strength_names <- grep(
    "^seasonal_strength",
    names(ts_feat),
    value = TRUE
  )

  list(
    values = as.numeric(values),
    periods = as.list(sort(periods)),
    mstl = list(
      trend = as.numeric(fit[, "Trend"]),
      seasonal = seasonals,
      remainder = as.numeric(fit[, "Remainder"])
    ),
    tsfeatures = list(
      trend = unname_or_na(ts_feat["trend"]),
      seasonal_strength = as.list(setNames(
        sapply(seasonal_strength_names, function(nm) unname_or_na(ts_feat[nm])),
        seasonal_strength_names
      ))
    )
  )
}

# Non-seasonal / short / degenerate case: stats::supsmu() trend (where
# applicable) + tsfeatures::stl_features() on a plain numeric vector (no ts
# class) so R's own "insufficient data" / non-seasonal fallback runs,
# matching what compute_ts_stats will do for seasonal_period = 1 / 0 with no
# usable period.
non_seasonal_case <- function(values, declared_period = NULL) {
  n <- length(values)
  supsmu_fit <- stats::supsmu(seq_len(n), values)

  # tsfeatures on a plain (non-ts) numeric vector forces the "else" branch of
  # stl_features (msts <- 1; nperiods <- 0L; season <- 0) and forecast::mstl's
  # non-seasonal branch (supsmu trend) -- exactly the case ts_stats hits when
  # seasonal_period resolves to "none usable".
  ts_feat <- tsfeatures::stl_features(as.numeric(values))

  list(
    values = as.numeric(values),
    periods = list(),
    declared_period = declared_period,
    supsmu = list(
      trend = as.numeric(supsmu_fit$y)
    ),
    tsfeatures = list(
      trend = unname_or_na(ts_feat["trend"]),
      seasonal_strength = NA
    )
  )
}

unname_or_na <- function(x) {
  v <- unname(x)
  if (length(v) == 0 || is.na(v)) {
    return(NA)
  }
  as.numeric(v)
}

# --- assemble cases ---------------------------------------------------------

cases <- list(
  audit_rw150 = single_period_case(audit_rw150_values, 12),
  air = single_period_case(air_values, frequency(s$air)),
  ref_rw200 = single_period_case(ref_rw200_values, 12),
  det_seasonal84 = single_period_case(det_seasonal84_values, 7),
  issue286_weekly84 = single_period_case(issue286_weekly84_values, 7),
  msts_7_28 = multi_period_case(msts_values, c(7, 28)),
  det_trend60 = non_seasonal_case(det_trend60_values, declared_period = 1),
  wn100 = non_seasonal_case(wn100_values, declared_period = NULL),
  short_seasonal14 = non_seasonal_case(short_seasonal14_values, declared_period = 7),
  constant_48 = non_seasonal_case(constant_48_values, declared_period = 12)
)

tv <- provenance("r_stl_strength", GENERATOR_PATH)$tool_versions
tv$forecast <- as.character(packageVersion("forecast"))
tv$tsfeatures <- as.character(packageVersion("tsfeatures"))

stl_strength_fixture <- list(
  family = "r_stl_strength",
  generator = GENERATOR_PATH,
  tool_versions = tv,
  tolerance = list(
    component_abs = 1e-8,
    strength_abs = 1e-8,
    supsmu_abs = 1e-8,
    notes = "same algorithms (stats::stl / stats::supsmu / forecast::mstl ported 1:1 from R's C/Fortran sources); only floating-point summation order differs"
  ),
  cases = cases
)

write_fixture(stl_strength_fixture, "r_stl_strength/cases.json")
