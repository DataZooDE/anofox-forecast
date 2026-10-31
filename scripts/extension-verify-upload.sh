#!/usr/bin/env bash
# scripts/extension-verify-upload.sh NAME ARCH DUCKDB_VERSION
#
# Post-upload check (#272, DIST-02), run in the deploy job (_extension_deploy.yml)
# directly after scripts/extension-upload.sh. For a WASM arch, downloads every
# object scripts/extension-upload.sh just uploaded (per its manifest,
# $EXT_DIR/$NAME.duckdb_extension.wasm.uploaded) and runs
# scripts/verify-wasm-artifact.sh on each. If nothing was uploaded in this run
# (branch/PR run, or no AWS key -- the manifest is empty or missing), falls
# back to verifying the locally-processed .compressed bytes instead, so the
# check still proves something on every run, not just on deploys.
#
# A non-wasm ARCH has nothing to verify here (the native truncate/replace path
# is unchanged from before this phase) and exits 0 immediately.
#
# This script runs inside the deploy job, which holds AWS OIDC credentials
# (#272 threat T-19-07) -- never enable shell command-echo tracing here, and
# never print environment variables.
#
# Usage: ./extension-verify-upload.sh NAME ARCH DUCKDB_VERSION
set -euo pipefail

if [ $# -ne 3 ]; then
  echo "usage: extension-verify-upload.sh NAME ARCH DUCKDB_VERSION" >&2
  exit 1
fi

NAME="$1"
ARCH="$2"
DUCKDB_VERSION="$3"

if [[ "$ARCH" != wasm* ]]; then
  echo "nothing to verify for non-wasm arch $ARCH"
  exit 0
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTENSION_UPLOAD_DIR="${EXTENSION_UPLOAD_DIR:-/tmp/extension}"

MANIFEST="$EXTENSION_UPLOAD_DIR/$NAME.duckdb_extension.wasm.uploaded"
COMPRESSED="$EXTENSION_UPLOAD_DIR/$NAME.duckdb_extension.wasm.compressed"

TMPDIR_LOCAL="$(mktemp -d)"
cleanup() { rm -rf "$TMPDIR_LOCAL"; }
trap cleanup EXIT

if [ -s "$MANIFEST" ]; then
  while IFS= read -r uri; do
    [ -z "$uri" ] && continue
    echo "verifying uploaded object: $uri"
    dl="$TMPDIR_LOCAL/downloaded.wasm.br"
    aws s3 cp "$uri" "$dl" --only-show-errors
    "$REPO_ROOT/scripts/verify-wasm-artifact.sh" "$dl" "$ARCH" "$DUCKDB_VERSION"
  done < "$MANIFEST"
else
  if [ ! -f "$COMPRESSED" ]; then
    echo "error: neither an upload manifest ($MANIFEST) nor the local compressed artifact ($COMPRESSED) exists" >&2
    exit 1
  fi
  echo "nothing was uploaded in this run (manifest empty/missing) -- verifying the would-be-uploaded local bytes instead: $COMPRESSED"
  "$REPO_ROOT/scripts/verify-wasm-artifact.sh" "$COMPRESSED" "$ARCH" "$DUCKDB_VERSION"
fi
