# Node 24 is the active LTS V8 runtime used by the shared frontend chart renderer.
FROM node:24-bookworm-slim AS node_runtime

# Use a Python image with uv pre-installed
FROM ghcr.io/astral-sh/uv:0.9.30-python3.9-bookworm-slim

COPY --from=node_runtime /usr/local/bin/node /usr/local/bin/node

# Build this Dockerfile from the repository root so the backend can execute the
# same JavaScript chart renderer as the Nuxt frontend.

# Setup a non-root user
RUN groupadd --system --gid 999 nonroot \
 && useradd --system --gid 999 --uid 999 --create-home nonroot

# Install the project into `/app`
WORKDIR /app/ML_backend

# Enable bytecode compilation
ENV UV_COMPILE_BYTECODE=1

# Copy from the cache instead of linking since it's a mounted volume
ENV UV_LINK_MODE=copy

# Omit development dependencies
ENV UV_NO_DEV=1

# Ensure installed tools can be executed out of the box
ENV UV_TOOL_BIN_DIR=/usr/local/bin

# PyCifRW (pulled in by orix/diffpy-structure on Python 3.9) is built from
# source, so install and remove the compiler toolchain in the same layer.
RUN --mount=type=cache,target=/root/.cache/uv \
    --mount=type=bind,source=ML_backend/uv.lock,target=uv.lock \
    --mount=type=bind,source=ML_backend/pyproject.toml,target=pyproject.toml \
    apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libcairo2 \
        libpango-1.0-0 \
        libpangocairo-1.0-0 \
    && uv sync --locked --no-install-project \
    && apt-get purge -y --auto-remove build-essential \
    && rm -rf /var/lib/apt/lists/*

# Then, add the rest of the project source code and install it
# Installing separately from its dependencies allows optimal layer caching
COPY ML_backend /app/ML_backend
COPY frontend/package.json /app/frontend/package.json
COPY frontend/utils/chartScale.js /app/frontend/utils/chartScale.js
COPY frontend/utils/grainChartRenderer.js /app/frontend/utils/grainChartRenderer.js
COPY frontend/tools/render_grain_charts.mjs /app/frontend/tools/render_grain_charts.mjs
RUN chmod 0644 /app/ML_backend/assets/ebsd_report_template.pptx
RUN --mount=type=cache,target=/root/.cache/uv \
    uv sync --locked

# Place executables in the environment at the front of the path
ENV PATH="/app/ML_backend/.venv/bin:$PATH"

# Reset the entrypoint, don't invoke `uv`
ENTRYPOINT []

# Use the non-root user to run our application
USER nonroot

# Run the FastAPI application by default
# Uses `uv run` to sync dependencies on startup, respecting UV_NO_DEV
# Uses `fastapi dev` to enable hot-reloading when the `watch` sync occurs
# Uses `--host 0.0.0.0` to allow access from outside the container
# Note in production, you should use `fastapi run` instead
CMD ["uv", "run", "uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
