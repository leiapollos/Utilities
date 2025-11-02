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

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(HOST_SOURCES
    ${SRC_ROOT}/main.mm
)

add_executable(utilities_host ${HOST_SOURCES})
target_include_directories(utilities_host PRIVATE ${SRC_ROOT})

if(APPLE)
    target_link_libraries(utilities_host PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal")
endif()

set_target_properties(utilities_host PROPERTIES OUTPUT_NAME "utilities_host")

add_library(utilities_app MODULE ${SRC_ROOT}/app.cpp)
target_include_directories(utilities_app PRIVATE ${SRC_ROOT})

if(APPLE)
    set_target_properties(utilities_app PROPERTIES SUFFIX ".dylib")
    target_link_options(utilities_app PRIVATE "-undefined" "dynamic_lookup")
endif()

set_target_properties(utilities_app PROPERTIES
    OUTPUT_NAME "utilities_app"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/hot"
    PREFIX ""
)

install(TARGETS utilities_host DESTINATION bin)
install(TARGETS utilities_app DESTINATION bin)