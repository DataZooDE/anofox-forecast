# R MSTL / classical-decomposition reference generator (Phase 15 plan
# 15-04, DET-04, D-09/D-10).
#
# Writes validation/reference/fixtures/r_decomposition/cases.json: R
# forecast::mstl() components (trend, per-period seasonal, remainder) for
# six `mstl` cases, and R stats::decompose() components (trend with NA,
# seasonal, random, figure) for five `decompose` cases -- consumed by
# crates/anofox-fcst-core/tests/mstl_decompose_reference.rs to pin
# decomposition::{mstl_fit, mstl_decompose, classical_decompose} against R.
#
# Regenerate with:
#   Rscript validation/reference/r/gen_decomposition_reference.R
# (works from any cwd -- the script resolves its own directory via --file=).

.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages({
  library(forecast)
})

GENERATOR_PATH <- "validation/reference/r/gen_decomposition_reference.R"

s <- reference_series()
air_values <- as.numeric(s$air)

# --- case input series --------------------------------------------------

det84_t <- 1:84
det84_values <- 10 + 3 * sin(2 * pi * det84_t / 7) + 0.1 * det84_t + 0.5 * cos(2.1 * det84_t)

msts_t <- 1:280
msts_values <- sin(2 * pi * msts_t / 7) + 0.5 * sin(2 * pi * msts_t / 28) + 0.3 * cos(1.7 * msts_t)

dropped_n60_values <- air_values[1:60]

set.seed(20261012)
nonseasonal_wn100_values <- rnorm(100)

ones48_values <- rep(1, 48)

air_n24_values <- air_values[1:24]

# --- helpers --------------------------------------------------------------

# MSTL case: forecast::mstl() trend/seasonal(s)/remainder. `periods` is the
# seasonal.periods vector passed to msts() (ascending input order is not
# assumed -- mstl() itself sorts/drops); for the non-seasonal case pass
# NULL to use a plain ts() (frequency 1) instead of msts().
mstl_case <- function(values, periods) {
  if (is.null(periods)) {
    fit <- forecast::mstl(ts(values))
    seasonal_cols <- character(0)
  } else {
    fit <- suppressWarnings(forecast::mstl(msts(values, seasonal.periods = periods)))
    seasonal_cols <- grep("^Seasonal", colnames(fit), value = TRUE)
  }

  seasonals <- lapply(seasonal_cols, function(nm) as.numeric(fit[, nm]))
  kept_periods <- as.numeric(sub("^Seasonal", "", seasonal_cols))
  # paste0() does NOT shortcut to character(0) just because one operand is a
  # zero-length numeric -- only when EVERY operand is zero-length. Guard
  # explicitly so the non-seasonal case (kept_periods = numeric(0)) doesn't
  # silently produce the single bogus name "p" (mismatched length vs the
  # zero-length `seasonals` list, which setNames() then rejects).
  seasonal_names <- if (length(kept_periods) == 0) character(0) else paste0("p", sort(kept_periods))

  list(
    values = as.numeric(values),
    requested_periods = if (is.null(periods)) list() else as.list(periods),
    kept_periods = as.list(sort(kept_periods)),
    mstl = list(
      trend = as.numeric(fit[, "Trend"]),
      seasonal = setNames(seasonals, seasonal_names),
      remainder = as.numeric(fit[, "Remainder"])
    )
  )
}

# Classical decompose case: R stats::decompose() trend (with NA)/seasonal/
# random/figure.
decompose_case <- function(values, period, type) {
  d <- decompose(ts(values, frequency = period), type = type)
  list(
    values = as.numeric(values),
    period = period,
    type = type,
    decompose = list(
      trend = as.numeric(d$trend),
      seasonal = as.numeric(d$seasonal),
      random = as.numeric(d$random),
      figure = as.numeric(d$figure)
    )
  )
}

# --- assemble cases ---------------------------------------------------------

mstl_cases <- list(
  air_p12 = mstl_case(air_values, 12),
  det84_p7 = mstl_case(det84_values, 7),
  msts_7_28 = mstl_case(msts_values, c(7, 28)),
  dropped_n60 = mstl_case(dropped_n60_values, c(12, 40)),
  nonseasonal_wn100 = mstl_case(nonseasonal_wn100_values, NULL),
  ones48_p12 = mstl_case(ones48_values, 12)
)

decompose_cases <- list(
  air_additive = decompose_case(air_values, 12, "additive"),
  air_multiplicative = decompose_case(air_values, 12, "multiplicative"),
  det84_additive = decompose_case(det84_values, 7, "additive"),
  ones48_additive = decompose_case(ones48_values, 12, "additive"),
  air_n24_additive = decompose_case(air_n24_values, 12, "additive")
)

tv <- provenance("r_decomposition", GENERATOR_PATH)$tool_versions
tv$forecast <- as.character(packageVersion("forecast"))

decomposition_fixture <- list(
  family = "r_decomposition",
  generator = GENERATOR_PATH,
  tool_versions = tv,
  tolerance = list(
    component_abs = 1e-8,
    classical_abs = 1e-10,
    notes = paste(
      "mstl: stl.rs/supsmu.rs are 1:1 ports of R's own stats::stl/stats::supsmu",
      "C/Fortran sources, so only floating-point summation order differs",
      "(component_abs 1e-8, matching r_stl_strength). classical: the centred",
      "moving-average filter and per-phase mean are simple sum/divide chains",
      "with no iterative fit, so classical_abs is tighter (1e-10)."
    )
  ),
  mstl_cases = mstl_cases,
  decompose_cases = decompose_cases
)

write_fixture(decomposition_fixture, "r_decomposition/cases.json")
