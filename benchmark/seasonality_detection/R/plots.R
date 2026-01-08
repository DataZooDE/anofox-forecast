# plots.R - Plotting functions for seasonality detection benchmark
# Requires: ggplot2, dplyr, tidyr, PRROC, scales

#' Plot Precision-Recall curves faceted by method family
#' @param pr_results List containing pr_data (data frame with Method, Recall, Precision)
#' @param colors Named vector of method colors
#' @param families Named vector mapping methods to families
plot_pr_curves_faceted <- function(pr_results, colors = METHOD_COLORS,
                                    families = METHOD_TO_FAMILY) {
  # Extract PR curve data
  pr_df <- pr_results %>%
    mutate(Family = families[Method])

  if (nrow(pr_df) == 0) return(NULL)

  # Calculate baseline (random classifier) for each family
  baseline_precision <- pr_df %>%
    group_by(Family) %>%
    summarise(baseline = mean(Precision[Recall == max(Recall)], na.rm = TRUE))

  ggplot(pr_df, aes(x = Recall, y = Precision, color = Method)) +
    geom_line(linewidth = 0.8) +
    facet_wrap(~Family, ncol = 3) +
    scale_color_manual(values = colors) +
    scale_x_continuous(limits = c(0, 1), breaks = seq(0, 1, 0.25)) +
    scale_y_continuous(limits = c(0, 1), breaks = seq(0, 1, 0.25)) +
    labs(
      x = "Recall (True Positive Rate)",
      y = "Precision (Positive Predictive Value)"
    ) +
    theme_minimal() +
    theme(
      legend.position = "bottom",
      strip.text = element_text(face = "bold", size = 11),
      panel.grid.minor = element_blank()
    ) +
    guides(color = guide_legend(nrow = 2))
}

#' Plot PR AUC comparison as horizontal bar chart
#' @param auc_data Data frame with Method and PR_AUC columns
#' @param colors Named vector of method colors
plot_pr_auc_comparison <- function(auc_data, colors = METHOD_COLORS) {
  auc_data <- auc_data %>%
    arrange(desc(PR_AUC)) %>%
    mutate(
      Method = factor(Method, levels = Method),
      Family = METHOD_TO_FAMILY[Method]
    )

  ggplot(auc_data, aes(x = Method, y = PR_AUC, fill = Method)) +
    geom_col() +
    geom_text(aes(label = sprintf("%.3f", PR_AUC)), hjust = -0.1, size = 3) +
    scale_fill_manual(values = colors) +
    scale_y_continuous(limits = c(0, 1.1), expand = c(0, 0)) +
    coord_flip() +
    labs(x = NULL, y = "Precision-Recall AUC") +
    theme_minimal() +
    theme(legend.position = "none")
}

#' Plot computational complexity (log-log scaling analysis)
#' @param timing_data Data frame with series_length and timing for each method
#' @param colors Named vector of method colors
plot_computational_scaling <- function(timing_data, colors = METHOD_COLORS) {
  ggplot(timing_data, aes(x = series_length, y = time_ms, color = Method)) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    scale_x_log10(labels = scales::comma) +
    scale_y_log10(labels = scales::comma) +
    scale_color_manual(values = colors) +
    labs(
      x = "Series Length (log scale)",
      y = "Execution Time in ms (log scale)",
      title = "Computational Complexity: Time vs Series Length"
    ) +
    theme_minimal() +
    theme(
      legend.position = "bottom",
      panel.grid.minor = element_blank()
    ) +
    guides(color = guide_legend(nrow = 2))
}

#' Plot runtime comparison as horizontal bar chart
#' @param runtime_data Data frame with Method and Runtime_ms columns
#' @param colors Named vector of method colors
plot_runtime_comparison <- function(runtime_data, colors = METHOD_COLORS) {
  runtime_data <- runtime_data %>%
    arrange(Runtime_ms) %>%
    mutate(Method = factor(Method, levels = Method))

  ggplot(runtime_data, aes(x = Method, y = Runtime_ms, fill = Method)) +
    geom_col() +
    geom_text(aes(label = sprintf("%.1f ms", Runtime_ms)),
              hjust = -0.1, size = 3) +
    scale_fill_manual(values = colors) +
    scale_y_continuous(expand = expansion(mult = c(0, 0.15))) +
    coord_flip() +
    labs(
      x = NULL,
      y = "Execution Time (ms per 1000 series)"
    ) +
    theme_minimal() +
    theme(legend.position = "none")
}

#' Plot example curves faceted by scenario
#' @param curve_data Data frame with scenario, t (time), and value columns
#' @param title Plot title
#' @param colors Optional named vector of scenario colors
plot_example_curves_faceted <- function(curve_data, title = "Example Curves",
                                         colors = NULL) {
  p <- ggplot(curve_data, aes(x = t, y = value)) +
    geom_line(linewidth = 0.6, color = "#333333") +
    facet_wrap(~scenario, ncol = 2, scales = "free_y") +
    labs(
      title = title,
      x = "Time",
      y = "Value"
    ) +
    theme_minimal() +
    theme(
      strip.text = element_text(face = "bold"),
      panel.grid.minor = element_blank()
    )

  if (!is.null(colors)) {
    p <- p + aes(color = scenario) +
      scale_color_manual(values = colors) +
      theme(legend.position = "none")
  }

  p
}

#' Plot challenge scenario comparison (faceted by scenario type)
#' @param results Data frame with scenario, Method, and metric columns
#' @param metric_col Name of the metric column (e.g., "PR_AUC", "F1")
#' @param colors Named vector of method colors
plot_challenge_comparison_faceted <- function(results, metric_col = "PR_AUC",
                                               colors = METHOD_COLORS) {
  ggplot(results, aes(x = Method, y = .data[[metric_col]], fill = Method)) +
    geom_col() +
    facet_wrap(~scenario, ncol = 2, scales = "free_y") +
    scale_fill_manual(values = colors) +
    labs(
      x = NULL,
      y = metric_col
    ) +
    theme_minimal() +
    theme(
      axis.text.x = element_text(angle = 45, hjust = 1, size = 7),
      legend.position = "none",
      strip.text = element_text(face = "bold")
    )
}

#' Plot confusion matrix as heatmap
#' @param confusion_matrix Matrix or table with TP, FP, TN, FN
#' @param method_name Name of the method for title
plot_confusion_matrix <- function(confusion_matrix, method_name) {
  cm_df <- as.data.frame(as.table(confusion_matrix))
  names(cm_df) <- c("Predicted", "Actual", "Count")

  total <- sum(cm_df$Count)
  cm_df$Percentage <- cm_df$Count / total * 100

  ggplot(cm_df, aes(x = Predicted, y = Actual, fill = Count)) +
    geom_tile(color = "white", linewidth = 1) +
    geom_text(aes(label = sprintf("%d\n(%.1f%%)", Count, Percentage)),
              size = 4, fontface = "bold") +
    scale_fill_gradient(low = "white", high = "#2166AC") +
    labs(
      title = method_name,
      x = "Predicted",
      y = "Actual"
    ) +
    theme_minimal() +
    theme(
      legend.position = "none",
      plot.title = element_text(hjust = 0.5, face = "bold"),
      axis.text = element_text(size = 11)
    )
}

#' Plot multiple confusion matrices in a faceted grid
#' @param predictions_list Named list of prediction data frames
#' @param methods Vector of method names to include
plot_confusion_matrices_faceted <- function(predictions_list, methods) {
  cm_data <- map_dfr(methods, function(method) {
    if (!method %in% names(predictions_list)) return(NULL)

    pred <- predictions_list[[method]]
    cm <- table(
      Predicted = factor(pred$predicted, levels = c("Non-Seasonal", "Seasonal")),
      Actual = factor(pred$actual, levels = c("Non-Seasonal", "Seasonal"))
    )

    as.data.frame(as.table(cm)) %>%
      mutate(Method = method)
  })

  if (nrow(cm_data) == 0) return(NULL)

  cm_data <- cm_data %>%
    group_by(Method) %>%
    mutate(
      Total = sum(Freq),
      Percentage = Freq / Total * 100
    ) %>%
    ungroup()

  ggplot(cm_data, aes(x = Predicted, y = Actual, fill = Freq)) +
    geom_tile(color = "white", linewidth = 1) +
    geom_text(aes(label = sprintf("%d\n(%.0f%%)", Freq, Percentage)),
              size = 3) +
    facet_wrap(~Method, ncol = 2) +
    scale_fill_gradient(low = "white", high = "#2166AC") +
    labs(x = "Predicted", y = "Actual") +
    theme_minimal() +
    theme(
      legend.position = "none",
      strip.text = element_text(face = "bold", size = 11)
    )
}

#' Plot False Positive Rate analysis for noise scenarios
#' @param fpr_data Data frame with Method, Scenario, and FPR columns
#' @param colors Named vector of method colors
plot_fpr_analysis <- function(fpr_data, colors = METHOD_COLORS) {
  method_order <- fpr_data %>%
    filter(grepl("Red|0.9", Scenario)) %>%
    arrange(FPR) %>%
    pull(Method) %>%
    unique()

  fpr_data <- fpr_data %>%
    mutate(Method = factor(Method, levels = method_order))

  ggplot(fpr_data, aes(x = Method, y = FPR, fill = Scenario)) +
    geom_col(position = position_dodge(width = 0.8), width = 0.7) +
    geom_hline(yintercept = 0.05, linetype = "dashed", color = "red",
               linewidth = 0.8) +
    annotate("text", x = Inf, y = 0.05, label = "5% target FPR",
             hjust = 1.1, vjust = -0.5, color = "red", size = 3) +
    scale_fill_brewer(palette = "Set2") +
    scale_y_continuous(labels = scales::percent, limits = c(0, NA)) +
    labs(
      x = NULL,
      y = "False Positive Rate",
      fill = "Noise Type"
    ) +
    theme_minimal() +
    theme(
      axis.text.x = element_text(angle = 45, hjust = 1),
      legend.position = "bottom"
    )
}

#' Plot failure cases (False Positives or False Negatives) - faceted
#' @param failed_cases Data frame with curve data and predictions
#' @param type "fp" for false positives, "fn" for false negatives
#' @param n_cases Number of cases to show (default 4)
plot_failure_cases <- function(failed_cases, type = c("fp", "fn"), n_cases = 4) {
  type <- match.arg(type)

  cases <- failed_cases %>%
    slice_head(n = n_cases)

  if (nrow(cases) == 0) return(NULL)

  plot_data <- cases %>%
    mutate(case_id = row_number()) %>%
    unnest(values) %>%
    group_by(case_id) %>%
    mutate(t = row_number() - 1) %>%
    ungroup()

  title <- if (type == "fp") {
    "False Positives: Non-seasonal series incorrectly classified"
  } else {
    "False Negatives: Seasonal series missed by detector"
  }

  color <- if (type == "fp") "#B2182B" else "#2166AC"

  ggplot(plot_data, aes(x = t, y = values)) +
    geom_line(color = color, linewidth = 0.6) +
    facet_wrap(~case_id, ncol = 2, scales = "free_y",
               labeller = labeller(case_id = function(x) paste("Case", x))) +
    labs(
      title = title,
      x = "Time",
      y = "Value"
    ) +
    theme_minimal() +
    theme(strip.text = element_text(face = "bold"))
}

#' Plot calibration analysis (threshold vs FPR)
#' @param calibration_data Data frame with Method, threshold, and FPR columns
#' @param colors Named vector of method colors
plot_calibration_curves <- function(calibration_data, colors = METHOD_COLORS) {
  ggplot(calibration_data, aes(x = threshold, y = FPR, color = Method)) +
    geom_line(linewidth = 0.8) +
    geom_hline(yintercept = 0.05, linetype = "dashed", color = "red") +
    scale_color_manual(values = colors) +
    scale_y_continuous(labels = scales::percent, limits = c(0, 1)) +
    labs(
      x = "Detection Threshold",
      y = "False Positive Rate",
      title = "Threshold Calibration: FPR vs Threshold"
    ) +
    theme_minimal() +
    theme(legend.position = "bottom")
}

#' Plot Fisher's g-test results comparison
#' @param fisher_results Data frame with Method, g_statistic, p_value, and detected columns
plot_fisher_comparison <- function(fisher_results) {
  fisher_long <- fisher_results %>%
    pivot_longer(cols = c(fft_fpr, fisher_fpr),
                 names_to = "Method",
                 values_to = "FPR") %>%
    mutate(Method = ifelse(Method == "fft_fpr", "FFT", "Fisher's g"))

  ggplot(fisher_long, aes(x = scenario, y = FPR, fill = Method)) +
    geom_col(position = position_dodge(width = 0.8), width = 0.7) +
    geom_hline(yintercept = 0.05, linetype = "dashed", color = "red") +
    scale_fill_manual(values = c("FFT" = "#B2182B", "Fisher's g" = "#2166AC")) +
    scale_y_continuous(labels = scales::percent) +
    labs(
      x = "Scenario",
      y = "False Positive Rate",
      title = "FFT vs Fisher's g-test: FPR Comparison"
    ) +
    theme_minimal() +
    theme(
      axis.text.x = element_text(angle = 45, hjust = 1),
      legend.position = "bottom"
    )
}

#' Plot M4 validation results
#' @param m4_results Data frame with Method, Simulation_F1, and M4_Recall columns
#' @param colors Named vector of method colors
plot_m4_validation <- function(m4_results, colors = METHOD_COLORS) {
  ggplot(m4_results, aes(x = Simulation_F1, y = M4_Recall, color = Method)) +
    geom_point(size = 4) +
    geom_text(aes(label = Method), hjust = -0.1, vjust = 0.5, size = 3) +
    geom_abline(slope = 1, intercept = 0, linetype = "dashed", color = "gray50") +
    scale_color_manual(values = colors) +
    scale_x_continuous(limits = c(0, 1)) +
    scale_y_continuous(limits = c(0, 1)) +
    labs(
      x = "Simulation F1 Score",
      y = "M4 Competition Recall",
      title = "Simulation vs Real-World Performance"
    ) +
    theme_minimal() +
    theme(legend.position = "none")
}

#' Plot trend robustness with dynamic caption data
#' @param trend_results Data frame with slope, Method, and metric columns
#' @param metric Name of metric column (default "PR_AUC")
#' @param colors Named vector of method colors
plot_trend_robustness <- function(trend_results, metric = "PR_AUC",
                                   colors = METHOD_COLORS) {
  ggplot(trend_results, aes(x = factor(slope), y = .data[[metric]],
                             color = Method, group = Method)) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    scale_color_manual(values = colors) +
    labs(
      x = "Trend Slope",
      y = metric
    ) +
    theme_minimal() +
    theme(legend.position = "bottom") +
    guides(color = guide_legend(nrow = 2))
}

#' Generate dynamic caption for trend robustness
#' @param trend_results Data frame with slope, Method, and metric columns
#' @param metric Name of metric column
generate_trend_caption <- function(trend_results, metric = "PR_AUC") {
  max_slope <- max(trend_results$slope)
  best_at_max <- trend_results %>%
    filter(slope == max_slope) %>%
    slice_max(.data[[metric]], n = 1)

  degradation <- trend_results %>%
    group_by(Method) %>%
    summarise(
      start_val = .data[[metric]][slope == min(slope)],
      end_val = .data[[metric]][slope == max(slope)],
      degradation = start_val - end_val,
      .groups = "drop"
    ) %>%
    slice_min(degradation, n = 1)

  sprintf(
    "Trend Robustness: %s maintains %.2f %s at slope %.1f. %s shows least degradation (%.0f%% drop).",
    best_at_max$Method, best_at_max[[metric]], metric, max_slope,
    degradation$Method, degradation$degradation * 100
  )
}
