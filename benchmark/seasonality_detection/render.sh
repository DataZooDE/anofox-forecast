#!/bin/bash
#
# Render the seasonality detection benchmark report
#
# Prerequisites:
#   - Quarto CLI (https://quarto.org/docs/get-started/)
#   - R with packages: DBI, duckdb, ggplot2, dplyr, tidyr, purrr, knitr, scales
#   - Built extension: make release
#
# Usage:
#   ./render.sh        # Render both HTML and PDF
#   ./render.sh html   # Render HTML only
#   ./render.sh pdf    # Render PDF only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check prerequisites
if ! command -v quarto &> /dev/null; then
    echo "ERROR: Quarto CLI not found. Install from https://quarto.org/docs/get-started/"
    exit 1
fi

if ! command -v R &> /dev/null; then
    echo "ERROR: R not found. Please install R."
    exit 1
fi

# Check extension is built
EXTENSION_PATH="../../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"
if [ ! -f "$EXTENSION_PATH" ]; then
    echo "ERROR: Extension not found at $EXTENSION_PATH"
    echo "Please build the extension first with 'make release' in the project root."
    exit 1
fi

# Determine output format
FORMAT="${1:-all}"

case "$FORMAT" in
    html)
        echo "Rendering HTML..."
        quarto render seasonality_detection_report.qmd --to html --output-dir _output
        echo "Done: _output/seasonality_detection_report.html"
        ;;
    pdf)
        echo "Rendering PDF..."
        quarto render seasonality_detection_report.qmd --to pdf --output-dir _output
        echo "Done: _output/seasonality_detection_report.pdf"
        ;;
    all)
        echo "Rendering HTML and PDF..."
        quarto render seasonality_detection_report.qmd --to html --output-dir _output
        quarto render seasonality_detection_report.qmd --to pdf --output-dir _output
        echo "Done: _output/seasonality_detection_report.html"
        echo "Done: _output/seasonality_detection_report.pdf"
        ;;
    *)
        echo "Usage: $0 [html|pdf|all]"
        exit 1
        ;;
esac
