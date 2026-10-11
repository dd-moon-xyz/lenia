ARG UV_VERSION=0.12.24
FROM ghcr.io/astral-sh/uv:${UV_VERSION} AS uv_source

FROM nvidia/cuda:12.9.2-devel-ubuntu24.04 AS cuda_source

FROM ubuntu:26.04 AS core

COPY --from=cuda_source /usr/local/cuda-12.9 /usr/local/cuda-12.9
ENV CUDA_HOME=/usr/local/cuda-12.9
ENV PATH="/usr/local/cuda-12.9/bin:${PATH}"
ENV LD_LIBRARY_PATH=/usr/local/cuda-12.9/lib64
ENV NVIDIA_VISIBLE_DEVICES=all
ENV NVIDIA_DRIVER_CAPABILITIES=compute,utility

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    python3.14 \
    python3.14-dev \
    build-essential \
    g++-14 \
    cmake \
    pkg-config \
    xorg-dev \
    libwayland-dev \
    libxkbcommon-dev \
    wayland-protocols \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Match Ubuntu 26.04 glibc's math exception specifications.
RUN sed -E -i \
    's/\b(rsqrtf?|sinpif?|cospif?)\((double|float) ([xa])\)/\1(\2 \3) noexcept(true)/g' \
    /usr/local/cuda-12.9/targets/*/include/crt/math_functions.h

ENV CC=gcc-14
ENV CXX=g++-14
ENV CUDAHOSTCXX=g++-14

COPY --from=uv_source /uv /uvx /bin/
ENV UV_LINK_MODE=copy
ENV UV_SYSTEM_PYTHON=1
RUN rm /usr/lib/python3.14/EXTERNALLY-MANAGED \
    && update-alternatives --install /usr/bin/python python /usr/bin/python3.14 1
WORKDIR /src
COPY pyproject.toml uv.lock .python-version ./
RUN --mount=type=cache,target=/root/.cache/uv \
    uv export --locked --no-dev --no-emit-project --format requirements-txt -o /tmp/requirements.txt \
    && uv pip install --system -r /tmp/requirements.txt \
    && rm /tmp/requirements.txt

FROM core AS devcontainer

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    git \
    openssh-client \
    vim \
    htop \
    zsh \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
ENV SHELL=/usr/bin/zsh

FROM core AS app

COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY source ./source
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --target lenia --parallel 2

COPY resources/animals_dim.csv resources/consolas.ttf ./resources/
COPY shaders ./shaders
COPY src ./src
RUN --mount=type=cache,target=/root/.cache/uv uv pip install --system -e . --no-deps
EXPOSE 9876
CMD ["uvicorn", "lenia.server.app:app", "--host", "0.0.0.0", "--port", "9876", "--workers", "1", "--ws-max-size", "65536"]
