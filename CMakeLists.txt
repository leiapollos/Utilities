cmake_minimum_required(VERSION 3.22)
project(Utils VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ADDRESS_SANITIZER "Enable Address Sanitizer" ON)

mark_as_advanced(
    ADDRESS_SANITIZER
    CMAKE_C_FLAGS
    CMAKE_CXX_FLAGS
    CMAKE_EXE_LINKER_FLAGS
)

add_executable(utils_app main.cpp)

install(TARGETS utils_app DESTINATION bin)