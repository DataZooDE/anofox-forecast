#!/bin/bash
# Patch Dockerfile to fix apk update command
# This is a temporary workaround until the fix is merged upstream in extension-ci-tools

set -e

DOCKERFILE="extension-ci-tools/docker/linux_amd64_musl/Dockerfile"

if [ -f "$DOCKERFILE" ]; then
    # Fix: apk update doesn't accept --y flag
    sed -i 's/apk update --y -qq/apk update -q/g' "$DOCKERFILE"
    echo "✅ Patched Dockerfile: Fixed apk update command"
else
    echo "⚠️  Dockerfile not found: $DOCKERFILE"
fi

