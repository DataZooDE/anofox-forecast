# External Integrations

**Analysis Date:** 2026-08-20

## APIs & External Services

**Analytics & Telemetry:**
- PostHog (SaaS) - Anonymous usage telemetry and feature tracking
  - SDK/Client: Custom C++ HTTP client via DuckDB's bundled `httplib` + OpenSSL
  - Endpoint: `https://eu.posthog.com/batch/`
  - API Key: `phc_t3wwRLtpyEmLHYaZCSszG0MqVr74J6wnCrj9D41zk2t`
  - Implementation: `posthog-telemetry/src/telemetry.cpp`, `src/anofox_forecast_extension.cpp`
  - Opt-out: `DATAZOO_DISABLE_TELEMETRY=1` environment variable or `SET anofox_telemetry_enabled = false` config
  - Auto-disable in CI: Detects `CI`, `GITHUB_ACTIONS`, `GITLAB_CI`, `CIRCLECI`, `TRAVIS`, `JENKINS_URL`, `BUILDKITE`, `TEAMCITY_VERSION`, `TF_BUILD`, `CODEBUILD_BUILD_ID` env vars
  - Events: `extension_load`, `function_execution` (queued asynchronously, survives connection closure)

**Time-Series Datasets (Benchmarking):**
- M4 Competition Dataset (via datasetsforecast) - Historical forecasting benchmark
  - Client: Python package `datasetsforecast>=0.0.8`
  - Usage: `benchmark/src/common/data.py` loads M4 training/test splits
- M5 Competition Dataset (via datasetsforecast) - Retail sales forecasting benchmark
  - Client: Python package `datasetsforecast>=0.0.8`
  - Usage: `benchmark/src/common/data.py` loads M5 training/test splits

## Data Storage

**Databases:**
- DuckDB v1.4.3+ (embedded/in-process)
  - Connection: Native C API for extension, SQL interface for users
  - Role: Time-series query engine and extension host
  - Client: Built-in `duckdb` Python package (>=1.5.1) in benchmarks

**File Storage:**
- Local filesystem only (no cloud integration in core extension)
- Benchmark artifacts: Local parquet files (`benchmark/*/results/*.parquet`)
- Optional S3 upload: Benchmarking only via `boto3` client if `S3_BUCKET` env var set (`benchmark/run_all.py`)

**Caching:**
- None detected in core extension
- Benchmark datasets cached locally via `datasetsforecast` package

## Authentication & Identity

**Auth Provider:**
- None (extension is self-contained, embedded in DuckDB)
- Telemetry: Anonymous distinct ID (SHA256 hash of MAC address or machine ID) sent with every event
  - MAC address detection: Platform-specific (Linux: `/sys/class/net/`, Windows: WMI via `iphlpapi.h`, macOS: `ifaddrs.h`)
  - Implementation: `posthog-telemetry/src/telemetry.cpp`, `PostHogTelemetry::GetMacAddress()`, `GetDistinctId()`
- S3 authentication: Via AWS SDK `boto3` (uses `.aws/credentials` or `AWS_ACCESS_KEY_ID`/`AWS_SECRET_ACCESS_KEY` env vars)

## Monitoring & Observability

**Error Tracking:**
- None (PostHog only tracks usage, not errors)

**Logs:**
- Native DuckDB logging via extension context
- No external log aggregation
- Benchmark suite: Console output via `tabulate>=0.9.0`

## CI/CD & Deployment

**Hosting:**
- DuckDB Community Extensions registry (duckdb/community-extensions)
- GitHub Releases for compiled binaries
- GitHub Pages for documentation

**CI Pipeline:**
- GitHub Actions (.github/workflows/MainDistributionPipeline.yml, _extension_deploy.yml)
- Builds cross-platform: Linux (x86_64/arm64), macOS (x86_64/arm64), Windows (x86_64/arm64)
- WASM support: Emscripten via extension-ci-tools workflow

## Environment Configuration

**Required env vars (Benchmark/S3 upload):**
- `S3_BUCKET` - S3 bucket name for benchmark result uploads (optional; skipped if not set)
- `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY` - AWS credentials (if S3 upload enabled)

**Optional env vars:**
- `DATAZOO_DISABLE_TELEMETRY` - Disable PostHog telemetry: `1`, `true`, or `yes`
- `CI` (and other CI env vars) - Auto-disable telemetry detection in continuous integration

**Secrets location:**
- AWS credentials: Standard AWS SDK locations (`~/.aws/credentials`, env vars)
- PostHog API key: Compiled into extension binary (visible via reverse engineering; not a secret)

## Webhooks & Callbacks

**Incoming:**
- None (extension does not expose webhook endpoints)

**Outgoing:**
- PostHog event batch HTTP POST to `https://eu.posthog.com/batch/`
  - Triggered on: Extension load, function execution (asynchronously queued)
  - Payload: JSON batch with event metadata, distinct ID, properties (extension version, DuckDB version, platform)
  - Failure handling: Silent (telemetry errors never propagate to user)

## Extensions & Auto-Load Dependencies

**Auto-loaded:**
- `json` extension (required for STRUCT parameter syntax in table macros)
  - Loaded via `ExtensionHelper::TryAutoLoadExtension()` in `src/anofox_forecast_extension.cpp:19`

---

*Integration audit: 2026-08-20*
