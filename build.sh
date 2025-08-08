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

echo "==> Configuring CMake..."
# Note: We pass all flags directly to the cmake command
cmake -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}" .

echo "==> Building..."
cmake --build "${BUILD_DIR}"

echo "==> Done. Artifacts in '${BUILD_DIR}/'"