ARG UV_VERSION=0.12.10
FROM ghcr.io/astral-sh/uv:${UV_VERSION} AS uv_source

FROM nvidia/cuda:12.6.3-devel-ubuntu24.04 AS core

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential \
    cmake \
    pkg-config \
    xorg-dev \
    libwayland-dev \
    libxkbcommon-dev \
    wayland-protocols \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

COPY --from=uv_source /uv /uvx /bin/
ENV UV_LINK_MODE=copy
ENV UV_PYTHON_INSTALL_DIR=/opt/python
WORKDIR /src
COPY pyproject.toml uv.lock .python-version ./
RUN --mount=type=cache,target=/root/.cache/uv uv sync --locked --no-dev
ENV PATH="/src/.venv/bin:${PATH}"

FROM core AS app

COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY source ./source
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --target lenia --parallel 2

COPY resources/animals_dim.csv resources/consolas.ttf ./resources/
COPY shaders ./shaders
COPY server ./server
EXPOSE 9876
CMD ["uvicorn", "server.app:app", "--host", "0.0.0.0", "--port", "9876", "--workers", "1", "--ws-max-size", "65536"]
