# colors.R - Color palette for seasonality detection benchmark
# User-specified palette for consistent method identification across plots

# Primary method colors - ensure methods are identifiable across all charts
METHOD_COLORS <- c(
  "AIC" = "#2166AC",
  "FFT" = "#B2182B",
  "ACF" = "#1B7837",
  "Variance" = "#E66101",
  "Spectral" = "#762A83",
  "Wavelet" = "#D95F02",
  "SAZED" = "#984EA3",
  "Autoperiod" = "#FF7F00",
  "CFD" = "#A65628",
  "Lomb" = "#66C2A5",
  "MatrixProfile" = "#FC8D62",
  "STL" = "#8DA0CB",
  "SSA" = "#E78AC3"
)

# Method labels for display (lowercase -> display name)
method_labels <- c(
  "aic" = "AIC",
  "fft" = "FFT",
  "acf" = "ACF",
  "variance" = "Variance",
  "spectral" = "Spectral",
  "wavelet" = "Wavelet",
  "sazed" = "SAZED",
  "autoperiod" = "Autoperiod",
  "cfd" = "CFD",
  "lomb" = "Lomb",
  "mp" = "MatrixProfile",
  "stl" = "STL",
  "ssa" = "SSA"
)

# Method families for grouped analysis (PR curve faceting)
METHOD_FAMILIES <- list(
  Spectral = c("FFT", "Lomb", "SAZED", "Spectral"),
  Decomposition = c("AIC", "STL", "SSA"),
  Pattern = c("ACF", "MatrixProfile", "Autoperiod", "CFD", "Variance", "Wavelet")
)

# Create family lookup for each method
METHOD_TO_FAMILY <- c(
  "FFT" = "Spectral",
  "Lomb" = "Spectral",
  "SAZED" = "Spectral",
  "Spectral" = "Spectral",
  "AIC" = "Decomposition",
  "STL" = "Decomposition",
  "SSA" = "Decomposition",
  "ACF" = "Pattern",
  "MatrixProfile" = "Pattern",
  "Autoperiod" = "Pattern",
  "CFD" = "Pattern",
  "Variance" = "Pattern",
  "Wavelet" = "Pattern"
)

# Family colors for grouped plots
FAMILY_COLORS <- c(
  "Spectral" = "#B2182B",
  "Decomposition" = "#2166AC",
  "Pattern" = "#1B7837"
)

# Gradient palettes for heatmaps
HEATMAP_GRADIENT <- c(
  low = "#d73027",    # Red (bad)
  mid = "#ffffbf",    # Yellow (neutral)
  high = "#1a9850"    # Green (good)
)

# Confusion matrix cell colors
CONFUSION_MATRIX_COLORS <- c(
  "TN" = "#4575B4",   # Blue - True Negative
  "FP" = "#D73027",   # Red - False Positive
  "FN" = "#FC8D59",   # Orange - False Negative
  "TP" = "#1A9850"    # Green - True Positive
)

# Challenge scenario colors for faceted plots
SCENARIO_COLORS <- c(
  "baseline" = "#333333",
  "trend_0.1" = "#FDAE61",
  "trend_0.3" = "#F46D43",
  "trend_0.5" = "#D73027",
  "ar_0.0" = "#ABD9E9",
  "ar_0.3" = "#74ADD1",
  "ar_0.5" = "#4575B4",
  "ar_0.7" = "#313695",
  "ar_0.9" = "#1A1A5E"
)
