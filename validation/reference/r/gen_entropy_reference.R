# R approximate-entropy reference generator (Phase 13 plan 13-01, STAT-03, D-03).
#
# Writes validation/reference/fixtures/r_entropy/approx_entropy.json —
# pracma::approx_entropy(x, edim = 2, r = 0.2*sd(x), elag = 1) reference values
# for six cases, consumed by crates/anofox-fcst-core/tests/stats_reference.rs
# to pin ts_stats's `entropy` column (Pincus 1991 approximate entropy).
#
# Regenerate with:
#   Rscript validation/reference/r/gen_entropy_reference.R
# (works from any cwd — the script resolves its own directory via --file=).

.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))

suppressMessages({
  library(pracma)
})

GENERATOR_PATH <- "validation/reference/r/gen_entropy_reference.R"

s <- reference_series()
set.seed(20261010)

cases_raw <- list(
  ar1_100 = s$ar1[1:100],
  rw_200 = s$rw,
  sine_120 = sin(2 * pi * (1:120) / 12),
  white_noise_150 = rnorm(150),
  short_5 = c(1, 3, 2, 5, 4),
  constant_30 = rep(7, 30)
)

apen_case <- function(x) {
  r <- 0.2 * sd(x)
  apen <- pracma::approx_entropy(x, edim = 2, r = r, elag = 1)
  list(
    values = as.numeric(x),
    n = length(x),
    m = 2,
    r = as.numeric(r),
    apen = as.numeric(apen)
  )
}

cases <- lapply(cases_raw, apen_case)

tv <- provenance("r_entropy", GENERATOR_PATH)$tool_versions
tv$pracma <- as.character(packageVersion("pracma"))

entropy_fixture <- list(
  family = "r_entropy",
  generator = GENERATOR_PATH,
  tool_versions = tv,
  tolerance = list(
    abs = 1e-12,
    notes = "same algorithm, IEEE doubles; only log/summation order differs"
  ),
  cases = cases
)

write_fixture(entropy_fixture, "r_entropy/approx_entropy.json")
