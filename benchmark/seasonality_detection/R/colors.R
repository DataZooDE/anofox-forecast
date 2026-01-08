# colors.R - Colorblind-friendly palette for seasonality detection benchmark
# Using Okabe-Ito palette as base with extensions for 13 methods

# Base Okabe-Ito colorblind-friendly palette
OKABE_ITO <- c(
  orange = "#E69F00",
  sky_blue = "#56B4E9",
  bluish_green = "#009E73",
  yellow = "#F0E442",
  blue = "#0072B2",
  vermillion = "#D55E00",
  reddish_purple = "#CC79A7",

black = "#000000"
)

# Extended palette for 13 detection methods
# Core 8 from Okabe-Ito + 5 from Paul Tol's palette
METHOD_COLORS <- c(
  "AIC" = "#0072B2",           # Blue

"FFT" = "#D55E00",           # Vermillion
  "ACF" = "#009E73",           # Bluish Green
  "Variance" = "#E69F00",      # Orange
  "Spectral" = "#CC79A7",      # Reddish Purple
  "Wavelet" = "#56B4E9",       # Sky Blue
  "SAZED" = "#F0E442",         # Yellow
  "Autoperiod" = "#999999",    # Gray
  "CFD" = "#882255",           # Wine (Paul Tol)
  "Lomb" = "#44AA99",          # Teal (Paul Tol)
  "MatrixProfile" = "#DDCC77", # Sand (Paul Tol)
  "STL" = "#117733",           # Green (Paul Tol)
  "SSA" = "#AA4499"            # Magenta (Paul Tol)
)

# Method labels for display
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

# Method families for grouped analysis
METHOD_FAMILIES <- list(
  spectral = c("FFT", "Lomb", "SAZED", "Spectral"),
  decomposition = c("AIC", "STL", "SSA"),
  pattern = c("ACF", "MatrixProfile", "Autoperiod", "CFD", "Variance", "Wavelet")
)

# Create family lookup for each method
METHOD_TO_FAMILY <- c(
  "FFT" = "Spectral Methods",
  "Lomb" = "Spectral Methods",
  "SAZED" = "Spectral Methods",
  "Spectral" = "Spectral Methods",
  "AIC" = "Decomposition Methods",
  "STL" = "Decomposition Methods",
  "SSA" = "Decomposition Methods",
  "ACF" = "Pattern/Autocorrelation",
  "MatrixProfile" = "Pattern/Autocorrelation",
  "Autoperiod" = "Pattern/Autocorrelation",
  "CFD" = "Pattern/Autocorrelation",
  "Variance" = "Pattern/Autocorrelation",
  "Wavelet" = "Pattern/Autocorrelation"
)

# Family colors for grouped plots
FAMILY_COLORS <- c(
  "Spectral Methods" = "#D55E00",
  "Decomposition Methods" = "#0072B2",
  "Pattern/Autocorrelation" = "#009E73"
)

# Gradient palettes for heatmaps
HEATMAP_GRADIENT <- c(
  low = "#d73027",    # Red (bad)
  mid = "#ffffbf",    # Yellow (neutral)
  high = "#1a9850"    # Green (good)
)

CONFUSION_MATRIX_COLORS <- c(
  "TN" = "#4575B4",   # Blue - True Negative
  "FP" = "#D73027",   # Red - False Positive
  "FN" = "#FC8D59",   # Orange - False Negative
  "TP" = "#1A9850"    # Green - True Positive
)
