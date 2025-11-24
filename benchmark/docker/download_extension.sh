#!/bin/bash
set -e

if [ "$DOWNLOAD_EXTENSION" != "true" ]; then
    echo "Skipping extension download (DOWNLOAD_EXTENSION is not 'true')"
    exit 0
fi

if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GITHUB_TOKEN is required when DOWNLOAD_EXTENSION is true"
    exit 1
fi

REPO_NAME="${REPO_NAME:-datazoode/anofox-forecast}"
WORKFLOW_NAME="Main Extension Distribution Pipeline"
ARTIFACT_NAME="anofox_forecast-linux_amd64.duckdb_extension" # Adjust based on actual artifact name pattern

echo "Downloading extension from $REPO_NAME (Workflow: $WORKFLOW_NAME)..."

# 1. Get the workflow ID
WORKFLOW_ID=$(curl -s -H "Authorization: token $GITHUB_TOKEN" \
    "https://api.github.com/repos/$REPO_NAME/actions/workflows" | \
    jq -r ".workflows[] | select(.name == \"$WORKFLOW_NAME\") | .id")

if [ -z "$WORKFLOW_ID" ] || [ "$WORKFLOW_ID" == "null" ]; then
    echo "Error: Could not find workflow '$WORKFLOW_NAME'"
    exit 1
fi

echo "Found Workflow ID: $WORKFLOW_ID"

# 2. Get the latest successful run ID for this workflow
RUN_ID=$(curl -s -H "Authorization: token $GITHUB_TOKEN" \
    "https://api.github.com/repos/$REPO_NAME/actions/workflows/$WORKFLOW_ID/runs?status=success&per_page=1" | \
    jq -r ".workflow_runs[0].id")

if [ -z "$RUN_ID" ] || [ "$RUN_ID" == "null" ]; then
    echo "Error: Could not find a successful run for workflow '$WORKFLOW_NAME'"
    exit 1
fi

echo "Found latest successful Run ID: $RUN_ID"

# 3. List artifacts for the run and find the one matching our target
ARTIFACT_URL=$(curl -s -H "Authorization: token $GITHUB_TOKEN" \
    "https://api.github.com/repos/$REPO_NAME/actions/runs/$RUN_ID/artifacts" | \
    jq -r ".artifacts[] | select(.name | contains(\"linux_amd64\")) | .archive_download_url" | head -n 1)

if [ -z "$ARTIFACT_URL" ] || [ "$ARTIFACT_URL" == "null" ]; then
    echo "Error: Could not find artifact matching 'linux_amd64' in run $RUN_ID"
    echo "Available artifacts:"
    curl -s -H "Authorization: token $GITHUB_TOKEN" \
        "https://api.github.com/repos/$REPO_NAME/actions/runs/$RUN_ID/artifacts" | \
        jq -r ".artifacts[].name"
    exit 1
fi

echo "Downloading artifact from: $ARTIFACT_URL"

# 4. Download the artifact (zip)
mkdir -p /app/extensions
curl -L -H "Authorization: token $GITHUB_TOKEN" -o /app/extensions/extension.zip "$ARTIFACT_URL"

# 5. Unzip
unzip -o /app/extensions/extension.zip -d /app/extensions/
rm /app/extensions/extension.zip

# 6. Find the extension file and rename/verify if necessary
# Assuming the zip contains the extension file directly or in a folder
FOUND_EXT=$(find /app/extensions -name "*.duckdb_extension" | head -n 1)

if [ -z "$FOUND_EXT" ]; then
    echo "Error: No .duckdb_extension file found in the downloaded artifact"
    ls -R /app/extensions
    exit 1
fi

echo "Successfully downloaded extension to: $FOUND_EXT"

# Exporting this for subsequent layers/scripts might be tricky in a single RUN step context,
# but the Dockerfile ENV or the runner script will look for it.

