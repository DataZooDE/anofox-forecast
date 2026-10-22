#!/usr/bin/env bash
# scripts/verify-wasm-artifact.sh FILE ARCH DUCKDB_VERSION
#
# Post-upload WASM verification (#272, DIST-02). Given one WASM artifact
# (either a raw .wasm module or a brotli-compressed .wasm.br download), this
# decompresses it if needed, asserts it carries exactly one duckdb_signature
# custom section (scripts/wasm_signature_sections.py check), and -- only for
# the engine/arch combination the DuckDB-Wasm harness can actually LOAD --
# runs the real Node harness against it with test/wasm/post_upload_smoke.test.
# For every other engine/arch combination it records that only the section
# count was verified and exits 0; it never silently skips a failure.
#
# This script runs inside the deploy job, which holds AWS OIDC credentials
# (#272 threat T-19-07) -- never enable shell command-echo tracing here, and
# never print environment variables.
#
# Usage: ./verify-wasm-artifact.sh FILE ARCH DUCKDB_VERSION
set -euo pipefail

if [ $# -ne 3 ]; then
  echo "usage: verify-wasm-artifact.sh FILE ARCH DUCKDB_VERSION" >&2
  exit 1
fi

FILE="$1"
ARCH="$2"
DUCKDB_VERSION="$3"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Engine bundled by the @duckdb/duckdb-wasm version pinned in
# test/wasm/package.json -- must equal the engine bundled by that pin (see
# test/wasm/README.md DEP-01: Version-verification procedure); bump together.
WASM_HARNESS_ENGINE=v1.5.6
# Archs the harness's eh bundle can actually LOAD. 19-04 revisits wasm_mvp.
LOAD_ARCHS="wasm_eh"

if [ ! -f "$FILE" ]; then
  echo "error: file not found: $FILE" >&2
  exit 1
fi

TMPDIR_LOCAL="$(mktemp -d)"
cleanup() { rm -rf "$TMPDIR_LOCAL"; }
trap cleanup EXIT

MODULE="$TMPDIR_LOCAL/module.wasm"

# A raw WASM module starts with the 4-byte magic \0asm. A brotli-compressed
# download (content-encoding: br) does not -- decompress it first.
if python3 -c "import sys; sys.exit(0 if open(sys.argv[1], 'rb').read(4) == b'\x00asm' else 1)" "$FILE"; then
  cp "$FILE" "$MODULE"
else
  if ! brotli -d -c "$FILE" > "$MODULE" 2>"$TMPDIR_LOCAL/brotli.err"; then
    echo "error: $FILE is neither a raw WASM module nor brotli-decompressible:" >&2
    cat "$TMPDIR_LOCAL/brotli.err" >&2
    exit 1
  fi
fi

if ! python3 "$REPO_ROOT/scripts/wasm_signature_sections.py" check "$MODULE"; then
  echo "error: $FILE failed the duckdb_signature section check" >&2
  exit 1
fi

arch_in_load_archs() {
  local needle="$1"
  for a in $LOAD_ARCHS; do
    if [ "$a" = "$needle" ]; then
      return 0
    fi
  done
  return 1
}

if arch_in_load_archs "$ARCH" && [ "$DUCKDB_VERSION" = "$WASM_HARNESS_ENGINE" ]; then
  if [ ! -d "$REPO_ROOT/test/wasm/node_modules" ]; then
    echo "error: $REPO_ROOT/test/wasm/node_modules is missing -- run: npm --prefix test/wasm ci --ignore-scripts" >&2
    exit 1
  fi
  if ! env -u AWS_ACCESS_KEY_ID -u AWS_SECRET_ACCESS_KEY -u AWS_SESSION_TOKEN \
      node "$REPO_ROOT/test/wasm/run.mjs" --ext "$MODULE" --file test/wasm/post_upload_smoke.test; then
    echo "error: $FILE failed to LOAD / pass test/wasm/post_upload_smoke.test in the DuckDB-Wasm Node harness" >&2
    exit 1
  fi
else
  MSG="LOAD check skipped for $ARCH $DUCKDB_VERSION: no @duckdb/duckdb-wasm build bundles this engine/arch (harness engine $WASM_HARNESS_ENGINE, LOAD archs $LOAD_ARCHS); duckdb_signature section count verified only"
  echo "$MSG"
  if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    echo "::notice::$MSG"
  fi
fi
