# anofox-time Examples

The examples are organised around two end-to-end scenarios that combine the library's core features.
Each scenario is available as a C++ program in this directory and as a Python script under
`python/examples/`.

## Scenario Overview

- **pipeline_forecasting**  
  Builds a full forecasting workflow: transform pipeline (Yeo–Johnson + standard scaling), auto-select
  across classical models, rolling backtests, and leaderboard style reporting with hold-out metrics.

- **monitoring_workflow**  
  Demonstrates production monitoring: online anomaly detection with MAD, changepoint localisation,
  DBSCAN-based segment screening, and rolling ARIMA backtests to quantify drift.

## Building & Running C++ Examples

```bash
# Configure and build (recommended)
make examples

# Execute both scenarios
make run-example

# Execute individual binaries from the build tree
./build/examples/pipeline_forecasting
./build/examples/monitoring_workflow
```

To build manually with CMake:

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON
cmake --build . --target pipeline_forecasting monitoring_workflow
./examples/pipeline_forecasting
./examples/monitoring_workflow
```

The consolidated examples are wired into `ctest` (`ctest -R "^example-"`) and are exercised as part
of `make test-example`.

## Python Examples

The matching Python walkthroughs live in `python/examples/`:

- `pipeline_forecasting.py`
- `monitoring_workflow.py`

Run them after building/installing the bindings (e.g. `make dev`):

```bash
python python/examples/pipeline_forecasting.py
python python/examples/monitoring_workflow.py
```

## Extending

To add a new scenario, drop a `.cpp` file into this directory, update `CMakeLists.txt` to register the
executable, and (optionally) mirror the workflow in `python/examples/` so both language entry points
stay aligned. All examples rely solely on the public headers shipped with `anofox-time`.
