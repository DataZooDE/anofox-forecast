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

h <- 12
levels <- c(80, 95)

models <- list(
  naive = fc_block(naive(air, h = h, level = levels), "naive(air, h = 12, level = c(80, 95))"),
  snaive = fc_block(snaive(air, h = h, level = levels), "snaive(air, h = 12, level = c(80, 95))"),
  rwf_drift = fc_block(
    rwf(air, h = h, drift = TRUE, level = levels),
    "rwf(air, h = 12, drift = TRUE, level = c(80, 95))"
  )
)

air_fixture <- c(
  provenance("r_forecast", GENERATOR_PATH),
  list(
    tolerance = list(
      closed_form_abs = 1e-8,
      model_point_rel = 0.01,
      interval_rel = 0.05,
      notes = paste(
        "closed_form_abs is exact (naive/snaive/rwf drift are closed-form);",
        "model_point_rel and interval_rel are parity targets for later",
        "phases (Phase 10 FCST-01, Phase 17 FCST-09), not asserted in Phase 9."
      )
    ),
    inputs = list(
      name = "AirPassengers",
      frequency = 12,
      start = "1949-01",
      values = as.numeric(air)
    ),
    parameters = list(
      horizon = h,
      levels = levels
    ),
    models = models
  )
)

write_fixture(air_fixture, "r_forecast/air_passengers.json")
