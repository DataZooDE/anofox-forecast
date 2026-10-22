#!/bin/bash

# Extension upload script

# Usage: ./extension-upload.sh <name> <extension_version> <duckdb_version> <architecture> <s3_bucket> <copy_to_latest> <copy_to_versioned>
# <name>                : Name of the extension
# <extension_version>   : Version (commit / version tag) of the extension
# <duckdb_version>      : Version (commit / version tag) of DuckDB
# <architecture>        : Architecture target of the extension binary
# <s3_bucket>           : S3 bucket to upload to
# <copy_to_latest>      : Set this as the latest version ("true" / "false", default: "false")
# <copy_to_versioned>   : Set this as a versioned version that will prevent its deletion

set -e

# Directory holding the downloaded build artifact. Defaults to the CI path
# (/tmp/extension); overridable for local dry runs against a scratch copy
# without touching the real CI layout.
EXTENSION_UPLOAD_DIR="${EXTENSION_UPLOAD_DIR:-/tmp/extension}"

if [[ $4 == wasm* ]]; then
  ext="$EXTENSION_UPLOAD_DIR/$1.duckdb_extension.wasm"
else
  ext="$EXTENSION_UPLOAD_DIR/$1.duckdb_extension"
fi

echo $ext

script_dir="$(dirname "$(readlink -f "$0")")"

# Artifact shape (#272, DIST-01/DIST-02): both native and WASM build
# artifacts already end with a complete `duckdb_signature` custom section —
# metadata plus a 256-byte signature placeholder, zero-filled when unsigned.
# For BOTH platforms we strip that 256-byte placeholder before computing the
# hash and re-append a fresh signature in its place; we never append a new
# section HEADER. For native binaries the placeholder is the file's last 256
# bytes directly. For WASM artifacts the placeholder is the last 256 bytes of
# the existing trailing `duckdb_signature` custom section — that section's
# own LEB128 size field already accounts for those 256 bytes, so truncating
# and re-appending exactly 256 signature bytes keeps the section (and the
# whole module) well-formed.
#
# A WASM artifact that does NOT already end with exactly one
# `duckdb_signature` section aborts here (set -e + non-zero exit from the
# guard below) rather than silently getting a section appended — porting
# anofox-statistics' conditional PR #193 fix was considered and rejected for
# this repo's locked design (19-RESEARCH.md Pitfall 5): the current
# extension-ci-tools WASM artifact always already carries the section, and a
# future artifact shape change should fail loudly here, not get silently
# patched into a different (and unverified) byte layout.
if [[ $4 == wasm* ]]; then
  python3 "$script_dir/wasm_signature_sections.py" check "$ext"
fi

cat $ext > $ext.append
truncate -s -256 $ext.append

# (Optionally) Sign binary
if [ "$DUCKDB_EXTENSION_SIGNING_PK" != "" ]; then
  echo "$DUCKDB_EXTENSION_SIGNING_PK" > private.pem
  $script_dir/../duckdb/scripts/compute-extension-hash.sh $ext.append > $ext.hash
  openssl pkeyutl -sign -in $ext.hash -inkey private.pem -pkeyopt digest:sha256 -out $ext.sign
  rm -f private.pem
fi

# Signature is always there, potentially defaulting to 256 zeros
truncate -s 256 $ext.sign

# append signature to extension binary
cat $ext.sign >> $ext.append

# Post-guard (#272): the processed WASM artifact must still carry exactly
# one duckdb_signature section, now ending at EOF with the freshly-appended
# signature bytes as its payload, before we ever compress or upload it.
if [[ $4 == wasm* ]]; then
  python3 "$script_dir/wasm_signature_sections.py" check "$ext.append"
fi

# compress extension binary
if [[ $4 == wasm_* ]]; then
  brotli < $ext.append > "$ext.compressed"
else
  gzip < $ext.append > "$ext.compressed"
fi

set -e

# Upload manifest (#272, DIST-02): truncated fresh for this invocation, then
# appended with one s3:// URI per successful upload below. The post-upload
# check (scripts/extension-verify-upload.sh) reads this to know exactly
# which objects this run actually published, with no need to re-derive the
# key-formula logic in a second place.
: > "$ext.uploaded"

# Abort if AWS key is not set
if [ -z "$AWS_ACCESS_KEY_ID" ]; then
    echo "No AWS key found, skipping.."
    exit 0
fi

# upload versioned version
if [[ $7 = 'true' ]]; then
  if [[ $4 == wasm* ]]; then
    dest="s3://$5/$1/$2/$3/$4/$1.duckdb_extension.wasm"
    aws s3 cp $ext.compressed "$dest" --acl public-read --content-encoding br --content-type="application/wasm"
  else
    dest="s3://$5/$1/$2/$3/$4/$1.duckdb_extension.gz"
    aws s3 cp $ext.compressed "$dest" --acl public-read
  fi
  echo "$dest" >> "$ext.uploaded"
fi

# upload to latest version
if [[ $6 = 'true' ]]; then
  if [[ $4 == wasm* ]]; then
    dest="s3://$5/$3/$4/$1.duckdb_extension.wasm"
    aws s3 cp $ext.compressed "$dest" --acl public-read --content-encoding br --content-type="application/wasm"
  else
    dest="s3://$5/$3/$4/$1.duckdb_extension.gz"
    aws s3 cp $ext.compressed "$dest" --acl public-read
  fi
  echo "$dest" >> "$ext.uploaded"
fi
