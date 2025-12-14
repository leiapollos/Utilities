#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_NAME="metagen"
OUTPUT_PATH="${BUILD_DIR}/${OUTPUT_NAME}"

if [[ "${1}" == "clean" ]]; then
    echo "Cleaning..."
    rm -rf "${BUILD_DIR}"
    echo "Done."
    exit 0
fi

FORCE=0
if [[ "${1}" == "force" ]] || [[ "${2}" == "force" ]]; then
    FORCE=1
fi

needs_rebuild() {
    if [ ! -f "${OUTPUT_PATH}" ]; then
        return 0
    fi
    
    for src in "${SCRIPT_DIR}"/*.cpp "${SCRIPT_DIR}"/*.hpp \
               "${SCRIPT_DIR}/nstl/base"/*.cpp "${SCRIPT_DIR}/nstl/base"/*.hpp \
               "${SCRIPT_DIR}/nstl/os"/*.cpp "${SCRIPT_DIR}/nstl/os"/*.hpp \
               "${SCRIPT_DIR}/nstl/os/core"/*.cpp "${SCRIPT_DIR}/nstl/os/core"/*.hpp \
               "${SCRIPT_DIR}/nstl/os/core/macos"/*.cpp "${SCRIPT_DIR}/nstl/os/core/macos"/*.hpp; do
        if [ -f "$src" ] && [ "$src" -nt "${OUTPUT_PATH}" ]; then
            return 0
        fi
    done
    
    return 1
}

if [ ${FORCE} -eq 0 ] && ! needs_rebuild; then
    exit 0
fi

CXX="${CXX:-clang++}"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic"
CXXFLAGS="$CXXFLAGS -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function"
CXXFLAGS="$CXXFLAGS -Wno-gnu-anonymous-struct -Wno-nested-anon-types"

if [[ "${1}" == "debug" ]]; then
    CXXFLAGS="-std=c++17 -g -O0 -DBUILD_DEBUG -Wall -Wextra"
    CXXFLAGS="$CXXFLAGS -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function"
    CXXFLAGS="$CXXFLAGS -Wno-gnu-anonymous-struct -Wno-nested-anon-types"
    echo "Building in debug mode..."
fi

mkdir -p "${BUILD_DIR}"

echo "Compiling metagen..."
${CXX} ${CXXFLAGS} \
    -I"${SCRIPT_DIR}" \
    -I"${SCRIPT_DIR}/nstl/base" \
    -I"${SCRIPT_DIR}/nstl/os" \
    -I"${SCRIPT_DIR}/nstl/os/core" \
    -I"${SCRIPT_DIR}/nstl/os/core/macos" \
    -pthread \
    "${SCRIPT_DIR}/meta_main.cpp" \
    -o "${OUTPUT_PATH}"

echo "Build complete: ${OUTPUT_PATH}"

echo ""
"${OUTPUT_PATH}" --help | head -1
