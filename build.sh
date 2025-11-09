#!/usr/bin/env bash

# --- Configuration ---
BUILD_DIR="build"

clean_partial() {
    if [ ! -d "${BUILD_DIR}" ]; then
        echo "==> Build directory '${BUILD_DIR}' not present. Nothing to clean."
        return 0
    fi

    echo "==> Cleaning host/module build artifacts..."
    rm -rf \
        "${BUILD_DIR}/CMakeFiles/utilities_host.dir" \
        "${BUILD_DIR}/CMakeFiles/utilities_app.dir" \
        "${BUILD_DIR}/hot"

    rm -f \
        "${BUILD_DIR}/utilities_host" \
        "${BUILD_DIR}/utilities_host.pdb"

    rm -rf "${BUILD_DIR}/utilities_host.dSYM"

    echo "==> Host/module artifacts cleaned. Dear ImGui cache preserved."
}

clean_hard() {
    echo "==> Performing hard clean..."
    rm -rf "${BUILD_DIR}"
    echo "==> Build directory cleaned."
}

case "${1}" in
    clean)
        clean_partial
        exit 0
        ;;
    hard-clean)
        clean_hard
        exit 0
        ;;
esac

FLAVOR=${1:-release}
REQUESTED_TARGET=${2:-all}
CLEAN_BUILD=${3:-false}
CMAKE_ARGS=()

# --- Build Flavors ---
COMMON_WARN_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-gnu-zero-variadic-macro-arguments -Wno-initializer-overrides"
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
    C_FLAGS+=" -fsanitize=address -fno-omit-frame-pointer -DDEBUG=1"
    CXX_FLAGS+=" -fsanitize=address -fno-omit-frame-pointer -DDEBUG=1"
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
CMAKE_ARGS+=("-DCMAKE_OBJCXX_FLAGS=${CXX_FLAGS}")
if [[ -n "${LD_FLAGS}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_EXE_LINKER_FLAGS=${LD_FLAGS}")
fi

# --- Execution ---
case "${CLEAN_BUILD}" in
    clean)
        clean_partial
        ;;
    hard-clean)
        clean_hard
        ;;
    true)
        clean_hard
        ;;
esac
echo "==> Ensuring build directory exists..."
mkdir -p "${BUILD_DIR}"

echo "==> Configuring CMake..."
# Always force out-of-source build using explicit -S/-B.
cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

if [ ! -d "${BUILD_DIR}" ]; then
  echo "Error: expected build directory '${BUILD_DIR}' to exist after configure." >&2
  exit 2
fi

case "${REQUESTED_TARGET}" in
  all)
    BUILD_CMD=(cmake --build "${BUILD_DIR}" -- -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4))
    ;;
  host)
    BUILD_CMD=(cmake --build "${BUILD_DIR}" --target utilities_host -- -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4))
    ;;
  module)
    BUILD_CMD=(cmake --build "${BUILD_DIR}" --target utilities_app -- -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4))
    ;;
  *)
    echo "Error: Unknown build target '${REQUESTED_TARGET}'" >&2
    echo "Available targets: all, host, module" >&2
    exit 3
    ;;
esac

echo "==> Building (${REQUESTED_TARGET})..."
BUILD_START=$(date +%s.%N)
"${BUILD_CMD[@]}"
BUILD_STATUS=$?
if [ ${BUILD_STATUS} -ne 0 ]; then
  exit ${BUILD_STATUS}
fi
BUILD_END=$(date +%s.%N)
BUILD_TIME=$(awk "BEGIN {printf \"%.2f\", $BUILD_END - $BUILD_START}")
echo "==> Compilation time: ${BUILD_TIME}s"

echo "==> Done. Artifacts in '${BUILD_DIR}/'"