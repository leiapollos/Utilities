#!/usr/bin/env bash

# --- Configuration ---
BUILD_DIR="build"
FLAVOR=${1:-release} # Default to "release" if no argument is given
CMAKE_ARGS=()

# --- Build Flavors ---
COMMON_WARN_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-gnu-zero-variadic-macro-arguments"
C_FLAGS="${COMMON_WARN_FLAGS}"
CXX_FLAGS="${COMMON_WARN_FLAGS}"
LD_FLAGS=""

echo "==> Selected build flavor: ${FLAVOR}"
case "${FLAVOR}" in
  release)
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Release")
    CMAKE_ARGS+=("-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON") # Enable LTO
    ;;

  debug)
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Debug")
    CMAKE_ARGS+=("-DADDRESS_SANITIZER=1")
    C_FLAGS+=" -fsanitize=address -fno-omit-frame-pointer"
    CXX_FLAGS+=" -fsanitize=address -fno-omit-frame-pointer"
    LD_FLAGS="-fsanitize=address"
    ;;

  *)
    echo "Error: Unknown build flavor '${FLAVOR}'" >&2
    echo "Available flavors: release, debug, asan" >&2
    exit 1
    ;;
esac

CMAKE_ARGS+=("-DCMAKE_C_FLAGS=${C_FLAGS}")
CMAKE_ARGS+=("-DCMAKE_CXX_FLAGS=${CXX_FLAGS}")
if [[ -n "${LD_FLAGS}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_EXE_LINKER_FLAGS=${LD_FLAGS}")
fi

# --- Execution ---
echo "==> Cleaning build directory..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo "==> Configuring CMake..."
# Always force out-of-source build using explicit -S/-B.
cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

if [ ! -d "${BUILD_DIR}" ]; then
  echo "Error: expected build directory '${BUILD_DIR}' to exist after configure." >&2
  exit 2
fi

echo "==> Building..."
BUILD_START=$(date +%s.%N)
cmake --build "${BUILD_DIR}" -- -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
BUILD_END=$(date +%s.%N)
BUILD_TIME=$(awk "BEGIN {printf \"%.2f\", $BUILD_END - $BUILD_START}")
echo "==> Compilation time: ${BUILD_TIME}s"

echo "==> Done. Artifacts in '${BUILD_DIR}/'"