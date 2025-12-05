# Dockerfile Fix: apk update --y Flag Issue

## Problem

The Dockerfile in `extension-ci-tools/docker/linux_amd64_musl/Dockerfile` contains an invalid command:

```dockerfile
RUN apk update --y -qq
```

**Error:** `ERROR: command line: unrecognized option 'y'`

**Root Cause:** `apk update` does not accept the `--y` flag. Only `apk add` accepts `--y` (to automatically answer yes).

## Solution

Change line 8 in `extension-ci-tools/docker/linux_amd64_musl/Dockerfile` from:
```dockerfile
RUN apk update --y -qq
```

To:
```dockerfile
RUN apk update -q
```

## Workarounds

Since `extension-ci-tools` is a submodule pointing to `duckdb/extension-ci-tools` (which we don't have write access to), we have three options:

### Option 1: Local Fix (Already Applied)

The fix has been applied locally in the submodule. This works for:
- ✅ Local Docker builds
- ✅ Local development

**Status:** Already done - the submodule has the fix at commit `6909e35`.

### Option 2: Patch Script for CI

A patch script is available at `.github/workflows/patch-dockerfile.sh` that can be run before Docker builds in CI.

**Note:** The current CI workflow uses reusable workflows from `extension-ci-tools`, so the patch script would need to be integrated into the workflow manually, or the fix needs to be applied upstream.

### Option 3: Submit PR to Upstream (Recommended)

Submit a pull request to `duckdb/extension-ci-tools` to fix the Dockerfile upstream. This is the cleanest long-term solution.

**Repository:** https://github.com/duckdb/extension-ci-tools

**File to fix:** `docker/linux_amd64_musl/Dockerfile` line 8

**Change:** `apk update --y -qq` → `apk update -q`

## Current Status

- ✅ Local fix applied in submodule (commit `6909e35`)
- ✅ Patch script created (`.github/workflows/patch-dockerfile.sh`)
- ⚠️  Submodule commit cannot be pushed (no write access to upstream)
- ⚠️  CI will fail until fix is applied upstream or patch is integrated

## Temporary CI Workaround

If you need CI to work immediately, you can:

1. Fork `duckdb/extension-ci-tools`
2. Apply the fix in your fork
3. Update the submodule URL in `.gitmodules` to point to your fork
4. Update the workflow to use your fork's version

Or wait for the fix to be merged upstream in `duckdb/extension-ci-tools`.

