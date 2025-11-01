cmake_minimum_required(VERSION 3.22)

if(APPLE)
    project(Utils VERSION 1.0.0 LANGUAGES CXX OBJCXX)
else()
    project(Utils VERSION 1.0.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ADDRESS_SANITIZER "Enable Address Sanitizer" ON)

mark_as_advanced(
    ADDRESS_SANITIZER
    CMAKE_C_FLAGS
    CMAKE_CXX_FLAGS
    CMAKE_EXE_LINKER_FLAGS
)

if(APPLE)
    add_executable(utils_app main.mm)
    target_link_libraries(utils_app PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal")
else()
    add_executable(utils_app main.cpp)
endif()

install(TARGETS utils_app DESTINATION bin)