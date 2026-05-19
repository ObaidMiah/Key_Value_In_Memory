# Multi-stage build: stage 1 installs toolchain + vcpkg and compiles the
# project; stage 2 is a minimal Ubuntu image containing only the resulting
# server/client binaries. Vcpkg is invoked manually for the manifest-install
# step so docker layer caching makes subsequent rebuilds (with no dep
# changes) fast.

FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      git \
      curl \
      zip \
      unzip \
      tar \
      pkg-config \
      autoconf \
      automake \
      libtool \
      python3 \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
 && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /app

# Copy the manifest first so dep install is cached independently of source.
# Let vcpkg auto-detect the host triplet (x64-linux on amd64, arm64-linux on
# Apple Silicon hosts running native arm64 containers).
COPY vcpkg.json ./
RUN $VCPKG_ROOT/vcpkg install

COPY CMakeLists.txt ./
COPY proto ./proto
COPY include ./include
COPY src ./src
COPY tests ./tests

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
 && cmake --build build --parallel

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /app/build/src/kvstore_server /usr/local/bin/kvstore_server
COPY --from=build /app/build/src/kvstore_client /usr/local/bin/kvstore_client

EXPOSE 50051
ENTRYPOINT ["/usr/local/bin/kvstore_server"]
CMD ["--port", "50051", "--node-id", "node-0"]
