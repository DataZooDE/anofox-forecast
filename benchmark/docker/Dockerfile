FROM python:3.11-slim

# Build arguments
ARG GITHUB_TOKEN
ARG DOWNLOAD_EXTENSION=false
ARG REPO_NAME=datazoode/anofox-forecast

# Environment variables
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONPATH=/app
ENV REPO_NAME=${REPO_NAME}
ENV DOWNLOAD_EXTENSION=${DOWNLOAD_EXTENSION}
ENV GITHUB_TOKEN=${GITHUB_TOKEN}

# Install system dependencies
RUN apt-get update && apt-get install -y \
    git \
    curl \
    jq \
    unzip \
    awscli \
    && rm -rf /var/lib/apt/lists/*

# Install uv
COPY --from=ghcr.io/astral-sh/uv:latest /uv /bin/uv

# Set working directory
WORKDIR /app/benchmark

# Copy dependency files first for caching
COPY pyproject.toml uv.lock ./

# Install Python dependencies
RUN uv sync --frozen

# Copy the benchmark source code
COPY . .

# Download extension if requested
# We run this as a separate step so it can leverage cache if args don't change, 
# but usually we want this to run if the arg changes.
# Since we moved the file to docker/download_extension.sh but the context is ..,
# we need to copy it from docker/download_extension.sh
COPY docker/download_extension.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/download_extension.sh
RUN /usr/local/bin/download_extension.sh

# Set the extension path env var if the file exists (determined at runtime of the container usually, 
# but here we set a default location if downloaded)
# Since ENV values are static at build time, we rely on the startup script or the python code 
# to find it if we downloaded it to a known location.
# download_extension.sh puts it in /app/extensions/

ENV ANOFOX_EXTENSION_PATH_CONTAINER=/app/extensions/anofox_forecast.duckdb_extension

# We'll use an entrypoint that checks if we downloaded the extension and sets the env var accordingly
# or just rely on the python script checking the known location if the env var isn't set.
# But simpler: If download succeeded, the file is there.

CMD ["uv", "run", "python", "run_all.py"]
