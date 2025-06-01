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

file(GLOB JOBSYSTEM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/JobSystem/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/JobSystem/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/nstl/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/nstl/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/nstl/*.hpp")

add_executable(utils_app main.cpp ${JOBSYSTEM_SOURCES}
        nstl/typedefs.h
        nstl/platform.h
        nstl/memory.h
        nstl/atomic.h
        nstl/cache.h
        nstl/typetraits.h)

install(TARGETS utils_app DESTINATION bin)