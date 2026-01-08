# functions.R - Detection wrappers and calibration functions
# Provides modular detect_* functions with error handling and null calibration

#' Calibrate detection threshold using null distribution (white noise)
#' This targets a specific False Positive Rate (FPR) by finding the score
#' threshold at the (1-target_fpr) percentile of the null distribution.
#'
#' @param con DuckDB connection
#' @param method_name Name of the detection method
#' @param score_expr SQL expression to extract the score
#' @param n_samples Number of white noise samples to generate (default 500)
#' @param target_fpr Target false positive rate (default 0.05)
#' @param series_length Length of each series (default 60)
#' @return Calibrated threshold value
calibrate_null_threshold <- function(con, method_name, score_expr,
                                      n_samples = 500, target_fpr = 0.05,
                                      series_length = 60) {
  tryCatch({
    # Generate white noise samples directly in DuckDB
    null_scores <- dbGetQuery(con, sprintf("
      WITH RECURSIVE noise_series AS (
        SELECT
          row_number() OVER () as series_id,
          list_transform(range(%d), x -> random_normal(0, 0.3)) as values
        FROM range(%d)
      )
      SELECT
        series_id,
        %s as score
      FROM noise_series
    ", series_length, n_samples, score_expr))

    # Remove NA/NaN values
    valid_scores <- null_scores$score[!is.na(null_scores$score) &
                                       !is.nan(null_scores$score) &
                                       !is.infinite(null_scores$score)]

    if (length(valid_scores) < 10) {
      warning(sprintf("Insufficient valid scores for %s calibration", method_name))
      return(0.5)  # Default threshold
    }

    # Find threshold at (1-target_fpr) percentile
    threshold <- quantile(valid_scores, 1 - target_fpr, na.rm = TRUE)

    cat(sprintf("Calibrated %s threshold: %.4f (targeting %.1f%% FPR)\n",
                method_name, threshold, target_fpr * 100))

    return(as.numeric(threshold))
  }, error = function(e) {
    warning(sprintf("Calibration failed for %s: %s", method_name, e$message))
    return(0.5)  # Default threshold
  })
}

#' Wrapper for AIC-based detection
#' @param values Numeric vector of time series values
#' @param con DuckDB connection
#' @return List with score and detected
detect_aic <- function(values, con) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT (ts_aic_period(%s::DOUBLE[])).r_squared as score
    ", values_str))
    list(score = result$score, detected = !is.na(result$score) && result$score > 0.5)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for FFT-based detection
#' @param values Numeric vector of time series values
#' @param con DuckDB connection
#' @return List with score and detected
detect_fft <- function(values, con) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT
        LEAST(1.0, (ts_estimate_period_fft(%s::DOUBLE[])).confidence / 100.0) as score
    ", values_str))
    list(score = result$score, detected = !is.na(result$score) && result$score > 0.3)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for ACF-based detection
#' @param values Numeric vector of time series values
#' @param con DuckDB connection
#' @return List with score and detected
detect_acf <- function(values, con) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT (ts_estimate_period_acf(%s::DOUBLE[])).confidence as score
    ", values_str))
    list(score = result$score, detected = !is.na(result$score) && result$score > 0.3)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for Variance strength detection
#' @param values Numeric vector of time series values
#' @param period Expected period
#' @param con DuckDB connection
#' @return List with score and detected
detect_variance <- function(values, period, con) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT ts_seasonal_strength(%s::DOUBLE[], %d, 'variance') as score
    ", values_str, period))
    list(score = result$score, detected = !is.na(result$score) && result$score > 0.3)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for Wavelet strength detection
#' @param values Numeric vector of time series values
#' @param period Expected period
#' @param con DuckDB connection
#' @return List with score and detected
detect_wavelet <- function(values, period, con) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT ts_seasonal_strength(%s::DOUBLE[], %d, 'wavelet') as score
    ", values_str, period))
    list(score = result$score, detected = !is.na(result$score) && result$score > 0.3)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for Matrix Profile detection with calibrated threshold
#' @param values Numeric vector of time series values
#' @param con DuckDB connection
#' @param threshold Calibrated threshold (from calibrate_null_threshold)
#' @return List with score and detected
detect_mp <- function(values, con, threshold = 0.5) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT (ts_matrix_profile_period(%s::DOUBLE[])).confidence as score
    ", values_str))
    list(score = result$score,
         detected = !is.na(result$score) && result$score > threshold)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Wrapper for SSA detection with calibrated threshold
#' @param values Numeric vector of time series values
#' @param con DuckDB connection
#' @param threshold Calibrated threshold (from calibrate_null_threshold)
#' @return List with score and detected
detect_ssa <- function(values, con, threshold = 0.5) {
  tryCatch({
    values_str <- paste0("[", paste(values, collapse = ","), "]")
    result <- dbGetQuery(con, sprintf("
      SELECT (ts_ssa_period(%s::DOUBLE[])).variance_explained as score
    ", values_str))
    list(score = result$score,
         detected = !is.na(result$score) && result$score > threshold)
  }, error = function(e) {
    list(score = NA_real_, detected = FALSE)
  })
}

#' Fisher's g-test for periodicity detection
#' Tests the null hypothesis that the time series is white noise
#' @param values Numeric vector of time series values
#' @return List with g_statistic, p_value, and detected
fishers_g_test <- function(values) {
  tryCatch({
    n <- length(values)

    # Compute periodogram (squared FFT amplitudes)
    fft_result <- fft(values - mean(values))
    periodogram <- (Mod(fft_result)^2) / n

    # Use only positive frequencies (up to Nyquist)
    n_freqs <- floor(n / 2)
    periodogram <- periodogram[2:(n_freqs + 1)]  # Exclude DC component

    # Fisher's g statistic: max periodogram / sum of periodogram
    g_stat <- max(periodogram) / sum(periodogram)

    # P-value approximation (Percival & Walden, 1993)
    # Uses the probability that max of n_freqs exponential(1) r.v.s exceeds g * n_freqs
    p_value <- 1 - (1 - exp(-n_freqs * g_stat))^n_freqs

    # Bonferroni-style correction (conservative)
    p_value <- min(1, p_value * n_freqs)

    list(
      g_statistic = g_stat,
      p_value = p_value,
      detected = p_value < 0.05  # Reject H0: white noise at 5% level
    )
  }, error = function(e) {
    list(g_statistic = NA_real_, p_value = NA_real_, detected = FALSE)
  })
}

#' Generate seasonal data with various transformations
#' Unified function to create baseline, trend, noise, or shape variants
#' @param n_curves Number of curves to generate
#' @param n_points Length of each series
#' @param period Seasonal period
#' @param strength Seasonal strength (0 to 1)
#' @param noise_sd Standard deviation of noise
#' @param trend_slope Slope for linear trend (default 0)
#' @param ar_coef AR(1) coefficient for colored noise (default 0)
#' @param wave_shape Shape function ("sine", "square", "triangle", "sawtooth")
#' @param amplitude_mod Amplitude modulation factor (1 = no modulation)
#' @return Tibble with curve_id, values, and metadata
generate_seasonal_data <- function(n_curves, n_points, period, strength,
                                    noise_sd = 0.3, trend_slope = 0,
                                    ar_coef = 0, wave_shape = "sine",
                                    amplitude_mod = 1) {
  map_dfr(1:n_curves, function(i) {
    t <- 0:(n_points - 1)
    phase <- runif(1, 0, 2 * pi)

    # Calculate amplitude from strength
    if (strength > 0 && strength < 1) {
      amplitude <- sqrt(strength * noise_sd^2 / (1 - strength))
    } else if (strength >= 1) {
      amplitude <- 10 * noise_sd
    } else {
      amplitude <- 0
    }

    # Apply amplitude modulation
    amplitude <- amplitude * amplitude_mod

    # Generate wave based on shape
    angle <- 2 * pi * t / period + phase
    seasonal <- switch(wave_shape,
      "sine" = amplitude * sin(angle),
      "square" = amplitude * sign(sin(angle)),
      "triangle" = amplitude * (2 * abs(2 * ((t / period) - floor((t / period) + 0.5))) - 1),
      "sawtooth" = amplitude * (2 * ((t / period) - floor((t / period) + 0.5))),
      amplitude * sin(angle)  # Default to sine
    )

    # Generate noise (white or AR(1))
    if (ar_coef > 0) {
      noise <- numeric(n_points)
      noise[1] <- rnorm(1, 0, noise_sd)
      for (j in 2:n_points) {
        noise[j] <- ar_coef * noise[j-1] + rnorm(1, 0, noise_sd * sqrt(1 - ar_coef^2))
      }
    } else {
      noise <- rnorm(n_points, 0, noise_sd)
    }

    # Add trend
    trend <- trend_slope * t

    # Combine components
    values <- seasonal + noise + trend

    tibble(
      curve_id = i,
      strength_level = strength,
      is_seasonal = strength >= 0.2,
      values = list(values)
    )
  })
}

#' Compute PR curve data for plotting
#' @param scores Vector of prediction scores
#' @param labels Binary labels (TRUE = positive class)
#' @return Data frame with Recall and Precision columns
compute_pr_curve <- function(scores, labels) {
  # Sort by score descending
  ord <- order(scores, decreasing = TRUE)
  scores <- scores[ord]
  labels <- labels[ord]

  # Calculate cumulative TP and FP
  tp <- cumsum(labels)
  fp <- cumsum(!labels)

  # Precision and Recall
  precision <- tp / (tp + fp)
  recall <- tp / sum(labels)

  tibble(
    Recall = c(0, recall, 1),
    Precision = c(1, precision, sum(labels) / length(labels))
  )
}

#' Benchmark computational complexity across series lengths
#' @param con DuckDB connection
#' @param methods Vector of method SQL expressions
#' @param method_names Vector of method display names
#' @param lengths Vector of series lengths to test
#' @param n_series Number of series to test at each length
#' @return Data frame with series_length, Method, and time_ms
benchmark_complexity <- function(con, methods, method_names,
                                  lengths = c(50, 100, 200, 500, 1000),
                                  n_series = 100) {
  results <- map_dfr(lengths, function(len) {
    map_dfr(seq_along(methods), function(i) {
      tryCatch({
        # Generate test data
        timing <- system.time({
          dbExecute(con, sprintf("
            WITH test_data AS (
              SELECT
                row_number() OVER () as id,
                list_transform(range(%d), x -> random_normal(0, 1) + 0.5 * sin(2 * pi() * x / 12)) as values
              FROM range(%d)
            )
            SELECT id, %s as result
            FROM test_data
          ", len, n_series, methods[i]))
        })

        tibble(
          series_length = len,
          Method = method_names[i],
          time_ms = timing["elapsed"] * 1000
        )
      }, error = function(e) {
        tibble(series_length = len, Method = method_names[i], time_ms = NA_real_)
      })
    })
  })

  results
}
