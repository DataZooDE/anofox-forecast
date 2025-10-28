PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=anofox_forecast
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Skip building jemalloc (not needed for this extension)
EXT_FLAGS=-DSKIP_EXTENSIONS="jemalloc"

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile