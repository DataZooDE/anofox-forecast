# anofox_forecast Telemetry

`anofox_forecast` collects **anonymous, privacy-preserving usage telemetry** so
we can see which capabilities are used, on which platforms, and where they fail
— and prioritise accordingly. It is **on by default** and **trivial to turn
off**.

Telemetry is emitted through the shared
[`DataZooDE/posthog-telemetry`](https://github.com/DataZooDE/posthog-telemetry)
library and follows the cross-product **`telemetry_schema: 2`** envelope
(`posthog-telemetry/TELEMETRY-SCHEMA.md`). Ingestion is the EU PostHog cloud.

## How to turn it off

Any one of these fully short-circuits telemetry — when disabled, **nothing
leaves the machine** (the opt-out is enforced at the transport, not just at the
call sites):

```sql
SET anofox_telemetry_enabled = false;   -- DuckDB setting (per session)
```

```bash
export DATAZOO_DISABLE_TELEMETRY=1       # environment (1|true|yes)
```

Telemetry is also auto-disabled when a CI environment is detected (`CI`,
`GITHUB_ACTIONS`, `GITLAB_CI`, and similar).

## The guarantee: bounded, enumerated, non-PII

Every property we send is **either** a constant drawn from a small,
code-controlled enumeration **or** a pure number (durations, counts). The
library additionally clamps every outgoing string to 512 bytes as a backstop.

We **never** send: table names, column names, series values, forecast inputs or
outputs, model parameters, `FILTER`/`WHERE` clauses, SQL text, row/result data,
or error messages. Only the fixed strings and numbers described below leave the
machine.

The instrumentation is centralised in the extension entry point
(`src/anofox_forecast_extension.cpp`) and the shared telemetry library header
(`posthog-telemetry/include/telemetry.hpp`).

## What is collected

### Envelope (attached to every event)

`product` (`anofox_forecast`), `product_version`, `product_edition` (`oss`),
`telemetry_schema` (`2`), `duckdb_version`, `os`, `arch`, `platform`, `is_ci`,
`is_container`, a per-process `$session_id`, and — once associated — the
`deployment` group. `distinct_id` is the SHA-256 of a machine id: a **stable,
pseudonymous** identifier, not tied to any personal data.

### Events

| Event | When | Properties (beyond the envelope) |
|---|---|---|
| `extension_loaded` | the `anofox_forecast` extension loads | — |
| `function_executed` | a DuckDB function runs — **aggregated** per function per session (not per row) | `function_name`, `call_count`, `duration_ms_p50` |

`extension_loaded` fires once, at extension load, from `LoadInternal`. The
`function_executed` aggregate is available for any instrumented function via
`RecordFunctionCall(function_name)`; this repository currently emits only the
`extension_loaded` envelope and adds no per-function instrumentation.

## Function-call aggregation

When a function is instrumented, DuckDB function calls are recorded via
`RecordFunctionCall(function_name)`, which aggregates in-process into a single
`function_executed` event per function per session (carrying `call_count` and
`duration_ms_p50`), flushed at session end. Instrumentation is placed at
bind/register time, never on a per-row `GetChunk` path, so a million-row scan
produces O(1) telemetry rows, not a firehose.

## Enterprise / account analytics

OSS `anofox_forecast` associates only the `deployment` group. It has no license
key, so no `account` group is associated.
