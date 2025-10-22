#!/usr/bin/env bash

# --- Configuration ---
BUILD_DIR="build"
FLAVOR=${1:-release} # Default to "release" if no argument is given
CMAKE_ARGS=()

# --- Build Flavors ---
echo "==> Selected build flavor: ${FLAVOR}"
case "${FLAVOR}" in
  release)
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Release")
    CMAKE_ARGS+=("-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON") # Enable LTO
    ;;

  debug)
    CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Debug")
    CMAKE_ARGS+=("-DADDRESS_SANITIZER=1")
    # Inject compiler and linker flags directly
    CMAKE_ARGS+=("-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer")
    CMAKE_ARGS+=("-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer")
    CMAKE_ARGS+=("-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address")
    ;;

  *)
    echo "Error: Unknown build flavor '${FLAVOR}'" >&2
    echo "Available flavors: release, debug, asan" >&2
    exit 1
    ;;
esac

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
cmake --build "${BUILD_DIR}" -- -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "==> Done. Artifacts in '${BUILD_DIR}/'"