# Shared helpers for the R reference-fixture generators.
#
# Every generator in validation/reference/r/ sources this file (after
# resolving its own script directory, since common.R is not loaded until
# `source()` runs). These helpers guarantee:
#   - fixtures are written to a cwd-independent path (script-dir relative)
#   - every fixture carries full provenance (family/generator/tool_versions)
#   - the same seeded reference series are used across every family
#   - no dates, hostnames or timestamps leak into fixture JSON, so
#     regeneration is byte-identical.

#' Resolve the directory containing the currently-running Rscript, regardless
#' of the working directory `Rscript` was invoked from.
ref_script_dir <- function() {
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  if (length(file_arg) == 0) {
    stop("ref_script_dir(): could not find --file= in commandArgs(); run via Rscript")
  }
  script_path <- sub("^--file=", "", file_arg[1])
  normalizePath(dirname(script_path), mustWork = TRUE)
}

#' Directory the fixtures are written to: <r-script-dir>/../fixtures
fixtures_dir <- function() {
  normalizePath(file.path(ref_script_dir(), "..", "fixtures"), mustWork = FALSE)
}

#' Build the provenance block every fixture must carry.
#' `generator` is the repo-relative path of the script that produced the
#' fixture (a literal string supplied by the caller, not derived) so the
#' provenance contract test can resolve it against repo_root().
provenance <- function(family, generator) {
  list(
    family = family,
    generator = generator,
    tool_versions = list(
      R = R.version.string,
      forecast = as.character(packageVersion("forecast")),
      tseries = as.character(packageVersion("tseries")),
      urca = as.character(packageVersion("urca")),
      jsonlite = as.character(packageVersion("jsonlite")),
      rng_kind = paste(RNGkind(), collapse = "/")
    )
  )
}

#' Deterministic seeded reference series shared by every R family.
#' Fixed RNGkind + seed + generation order guarantees byte-identical
#' regeneration across runs and across generator scripts.
reference_series <- function() {
  RNGkind("Mersenne-Twister", "Inversion", "Rejection")
  set.seed(20261008)

  air <- AirPassengers
  rw <- cumsum(rnorm(200))
  ar1 <- as.numeric(arima.sim(list(ar = 0.7), n = 300))
  inter <- ifelse(runif(120) < 0.3, rpois(120, 3) + 1, 0)

  list(air = air, rw = rw, ar1 = ar1, inter = inter)
}

#' Extract a fixture-ready block from a `forecast` object: the R call that
#' produced it (as a string, for provenance/debugging), the point forecast,
#' and — when present — the 80%/95% prediction interval bounds.
fc_block <- function(f, r_call) {
  block <- list(
    r_call = r_call,
    point = as.numeric(f$mean)
  )
  if (!is.null(f$lower)) {
    # Positional, not by column name: most models label columns "80%"/"95%",
    # but thetaf() labels them "Series 1"/"Series 2". Every caller in this
    # harness passes level = c(80, 95), so column 1 is always the 80% bound
    # and column 2 the 95% bound, regardless of label.
    block$lower_80 <- as.numeric(f$lower[, 1])
    block$upper_80 <- as.numeric(f$upper[, 1])
    block$lower_95 <- as.numeric(f$lower[, 2])
    block$upper_95 <- as.numeric(f$upper[, 2])
  }
  block
}

#' Write a fixture object to `<fixtures_dir()>/<rel_path>` as pretty JSON.
#' jsonlite encodes non-finite numerics as the strings "NA"/"NaN"/"Inf"/
#' "-Inf" — the Rust loader (crates/anofox-fcst-core/tests/common/mod.rs)
#' accepts exactly these spellings.
write_fixture <- function(obj, rel_path) {
  full_path <- file.path(fixtures_dir(), rel_path)
  dir.create(dirname(full_path), recursive = TRUE, showWarnings = FALSE)
  jsonlite::write_json(obj, full_path, auto_unbox = TRUE, digits = NA, pretty = TRUE)
  invisible(full_path)
}
