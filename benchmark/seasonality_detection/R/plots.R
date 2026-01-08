# plots.R - Plotting functions for seasonality detection benchmark
# Requires: ggplot2, dplyr, tidyr, pROC, scales

#' Plot ROC curves faceted by method family
#' @param roc_data List of ROC objects from pROC
#' @param colors Named vector of method colors
#' @param families Named vector mapping methods to families
plot_roc_curves_faceted <- function(roc_data, colors = METHOD_COLORS,
                                     families = METHOD_TO_FAMILY) {
  # Extract ROC curve data for each method
  roc_df <- map_dfr(names(roc_data), function(method) {
    roc_obj <- roc_data[[method]]
    if (is.null(roc_obj)) return(NULL)

    tibble(
      Method = method,
      Family = families[method],
      Specificity = roc_obj$specificities,
      Sensitivity = roc_obj$sensitivities
    )
  })

  if (nrow(roc_df) == 0) return(NULL)

  ggplot(roc_df, aes(x = 1 - Specificity, y = Sensitivity, color = Method)) +
    geom_line(linewidth = 0.8) +
    geom_abline(linetype = "dashed", color = "gray50") +
    facet_wrap(~Family, ncol = 3) +
    scale_color_manual(values = colors) +
    labs(
      x = "False Positive Rate (1 - Specificity)",
      y = "True Positive Rate (Sensitivity)"
    ) +
    theme_minimal() +
    theme(
      legend.position = "bottom",
      strip.text = element_text(face = "bold")
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

#' Plot confusion matrix as heatmap
#' @param confusion_matrix Matrix or table with TP, FP, TN, FN
#' @param method_name Name of the method for title
plot_confusion_matrix <- function(confusion_matrix, method_name) {
  # Convert to data frame for ggplot
  cm_df <- as.data.frame(as.table(confusion_matrix))
  names(cm_df) <- c("Predicted", "Actual", "Count")

  # Calculate percentages
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
  # Calculate confusion matrix data for each method
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

  # Calculate percentages within each method
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
#' @param fpr_data Data frame with Method, Scenario (white/red), and FPR columns
#' @param colors Named vector of method colors
plot_fpr_analysis <- function(fpr_data, colors = METHOD_COLORS) {
  # Order methods by FPR in red noise scenario
  method_order <- fpr_data %>%
    filter(Scenario == "Red Noise (phi=0.9)") %>%
    arrange(FPR) %>%
    pull(Method)

  fpr_data <- fpr_data %>%
    mutate(Method = factor(Method, levels = method_order))

  ggplot(fpr_data, aes(x = Method, y = FPR, fill = Scenario)) +
    geom_col(position = position_dodge(width = 0.8), width = 0.7) +
    geom_hline(yintercept = 0.05, linetype = "dashed", color = "red",
               linewidth = 0.8) +
    annotate("text", x = Inf, y = 0.05, label = "5% threshold",
             hjust = 1.1, vjust = -0.5, color = "red", size = 3) +
    scale_fill_manual(values = c("White Noise (phi=0)" = "#56B4E9",
                                  "Red Noise (phi=0.9)" = "#D55E00")) +
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

#' Plot failure cases (False Positives or False Negatives)
#' @param failed_cases Data frame with curve data and predictions
#' @param type "fp" for false positives, "fn" for false negatives
#' @param n_cases Number of cases to show (default 4)
plot_failure_cases <- function(failed_cases, type = c("fp", "fn"), n_cases = 4) {
  type <- match.arg(type)

  # Select subset of cases
  cases <- failed_cases %>%
    slice_head(n = n_cases)

  if (nrow(cases) == 0) return(NULL)

  # Unnest values and prepare for plotting
  plot_data <- cases %>%
    mutate(case_id = row_number()) %>%
    unnest(values) %>%
    group_by(case_id) %>%
    mutate(t = row_number() - 1) %>%
    ungroup()

  title <- if (type == "fp") {
    "False Positives: Non-seasonal series incorrectly classified as seasonal"
  } else {
    "False Negatives: Seasonal series missed by the detector"
  }

  ggplot(plot_data, aes(x = t, y = values)) +
    geom_line(color = if (type == "fp") "#D55E00" else "#0072B2",
              linewidth = 0.6) +
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

#' Plot trend robustness with dynamic caption data
#' @param trend_results Data frame with slope, Method, and AUC columns
#' @param colors Named vector of method colors
plot_trend_robustness <- function(trend_results, colors = METHOD_COLORS) {
  ggplot(trend_results, aes(x = factor(slope), y = AUC, color = Method, group = Method)) +
    geom_line(linewidth = 0.8) +
    geom_point(size = 2) +
    scale_color_manual(values = colors) +
    labs(
      x = "Trend Slope",
      y = "AUC"
    ) +
    theme_minimal() +
    theme(legend.position = "bottom") +
    guides(color = guide_legend(nrow = 2))
}

#' Generate dynamic caption for trend robustness
#' @param trend_results Data frame with slope, Method, and AUC columns
generate_trend_caption <- function(trend_results) {
  # Find best performing method at highest slope
  max_slope <- max(trend_results$slope)
  best_at_max <- trend_results %>%
    filter(slope == max_slope) %>%
    slice_max(AUC, n = 1)

  # Find method with least degradation
  degradation <- trend_results %>%
    group_by(Method) %>%
    summarise(
      start_auc = AUC[slope == min(slope)],
      end_auc = AUC[slope == max(slope)],
      degradation = start_auc - end_auc,
      .groups = "drop"
    ) %>%
    slice_min(degradation, n = 1)

  sprintf(
    "Trend Robustness: %s maintains %.2f AUC at slope %.1f. %s shows least degradation (%.0f%% drop).",
    best_at_max$Method, best_at_max$AUC, max_slope,
    degradation$Method, degradation$degradation * 100
  )
}

#' Plot AUC comparison bar chart
#' @param auc_data Data frame with Method and AUC columns
#' @param colors Named vector of method colors
plot_auc_comparison <- function(auc_data, colors = METHOD_COLORS) {
  auc_data <- auc_data %>%
    arrange(desc(AUC)) %>%
    mutate(Method = factor(Method, levels = Method))

  ggplot(auc_data, aes(x = Method, y = AUC, fill = Method)) +
    geom_col() +
    geom_text(aes(label = sprintf("%.3f", AUC)), vjust = -0.3, size = 3) +
    scale_fill_manual(values = colors) +
    scale_y_continuous(limits = c(0, 1.05), expand = c(0, 0)) +
    labs(x = NULL, y = "ROC AUC") +
    theme_minimal() +
    theme(
      axis.text.x = element_text(angle = 45, hjust = 1),
      legend.position = "none"
    )
}
