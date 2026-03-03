# ─── Stage 1: Builder ────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_DEFAULT_TRIPLET=x64-linux-dynamic

# 安裝建置工具與 vcpkg 所需系統套件
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    curl \
    zip \
    unzip \
    tar \
    ca-certificates \
    build-essential \
    gcc-13 \
    g++-13 \
    cmake \
    ninja-build \
    pkg-config \
    python3 \
    autoconf \
    automake \
    libtool \
    libgfortran5 \
    gfortran \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && rm -rf /var/lib/apt/lists/*

# 安裝 vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

# 複製專案原始碼
WORKDIR /app
COPY CMakeLists.txt CMakePresets.json vcpkg.json vcpkg-configuration.json ./
COPY include/ include/
COPY src/ src/

# 使用 vcpkg 安裝相依套件並建置（Release 模式）
RUN cmake --preset linux-release \
    && cmake --build build/linux-release --config Release

# ─── Stage 2: Runtime ────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgfortran5 \
    libgomp1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 複製執行檔
COPY --from=builder /app/build/linux-release/ebsd_cpp_backend /app/ebsd_cpp_backend

# 複製動態函式庫（x64-linux-dynamic triplet）
COPY --from=builder /app/build/linux-release/vcpkg_installed/x64-linux-dynamic/lib/ /app/lib/
COPY --from=builder /app/build/linux-release/vcpkg_installed/x64-linux-dynamic/bin/ /app/bin/

ENV LD_LIBRARY_PATH=/app/lib:/app/bin:${LD_LIBRARY_PATH}

# # 資料掛載點
# VOLUME ["/data", "/out"]

ENTRYPOINT ["/app/ebsd_cpp_backend"]
