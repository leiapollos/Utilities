cmake_minimum_required(VERSION 3.31)

project(Utils VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported)
if(supported)
    message(STATUS "IPO / LTO enabled")
    # Enable LTO for all targets created after this point
#    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported by the compiler.")
endif()

add_executable(utils_app main.cpp)

target_compile_options(utils_app PRIVATE
        -fsanitize=address
        -fno-omit-frame-pointer
)
# link-time flag (requires CMake ≥3.13)
target_link_options(utils_app PRIVATE
        -fsanitize=address
)

install(TARGETS utils_app DESTINATION bin)