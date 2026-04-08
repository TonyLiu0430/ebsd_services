# ─── Stage 1: Builder ────────────────────────────────────────────────────────
FROM ubuntu:24.04@sha256:84e77dee7d1bc93fb029a45e3c6cb9d8aa4831ccfcc7103d36e876938d28895b AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

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
COPY vcpkg-triplets/ vcpkg-triplets/

RUN ${VCPKG_ROOT}/vcpkg install \
    --triplet=linux-dynamic \
    --overlay-triplets=/app/vcpkg-triplets

COPY include/ include/
COPY src/ src/

RUN cmake --preset release \
    && cmake --build "build/release" --config Release \
    && mkdir -p /opt/runtime-app /opt/runtime-lib /opt/runtime-bin \
    && cp "/app/build/release/ebsd_cpp_backend" /opt/runtime-app/ebsd_cpp_backend \
    && cp -a "/app/build/release/vcpkg_installed/linux-dynamic/lib/." /opt/runtime-lib/ \
    && if [ -d "/app/build/release/vcpkg_installed/linux-dynamic/bin" ]; then cp -a "/app/build/release/vcpkg_installed/linux-dynamic/bin/." /opt/runtime-bin/; fi

# ─── Stage 2: Runtime ────────────────────────────────────────────────────────
FROM ubuntu:24.04@sha256:84e77dee7d1bc93fb029a45e3c6cb9d8aa4831ccfcc7103d36e876938d28895b AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgfortran5 \
    libgomp1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 複製執行檔
COPY --from=builder /opt/runtime-app/ebsd_cpp_backend /app/ebsd_cpp_backend

# 複製動態函式庫
COPY --from=builder /opt/runtime-lib/ /app/lib/
COPY --from=builder /opt/runtime-bin/ /app/bin/

ENV LD_LIBRARY_PATH=/app/lib:/app/bin:${LD_LIBRARY_PATH}

# # 資料掛載點
# VOLUME ["/data", "/out"]

ENTRYPOINT ["/app/ebsd_cpp_backend"]
