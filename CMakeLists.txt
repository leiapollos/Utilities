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

set(VULKAN_SDK_ROOT "${SRC_ROOT}/third_party/vulkan")
set(VULKAN_SDK_INCLUDE_DIR "${VULKAN_SDK_ROOT}/include")
set(VULKAN_VMA_INCLUDE_DIR "${SRC_ROOT}/third_party/vulkan_vma")
set(IMGUI_INCLUDE_DIR "${SRC_ROOT}/third_party/dear_imgui")

set(IMGUI_SOURCES
    ${IMGUI_INCLUDE_DIR}/imgui_unity.cpp
)

add_library(dear_imgui STATIC ${IMGUI_SOURCES})
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set_property(TARGET dear_imgui PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
endif()
set_target_properties(dear_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(dear_imgui
    PUBLIC
        ${IMGUI_INCLUDE_DIR}
    PRIVATE
        ${VULKAN_SDK_INCLUDE_DIR}
)

set(HOST_SOURCES
    ${SRC_ROOT}/main.mm
)

add_executable(utilities_host ${HOST_SOURCES})
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set_property(TARGET utilities_host PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
endif()
target_include_directories(utilities_host PRIVATE ${SRC_ROOT} ${VULKAN_VMA_INCLUDE_DIR} ${IMGUI_INCLUDE_DIR})
target_link_libraries(utilities_host PRIVATE dear_imgui)

if(APPLE)
    set(VULKAN_SDK_LIB_DIR "${VULKAN_SDK_ROOT}/macos/lib")

    add_library(VulkanSDK::Vulkan SHARED IMPORTED)
    set_target_properties(VulkanSDK::Vulkan PROPERTIES
        IMPORTED_LOCATION "${VULKAN_SDK_LIB_DIR}/libvulkan.dylib"
        IMPORTED_SONAME "libvulkan.1.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_SDK_INCLUDE_DIR}"
    )

    add_library(VulkanSDK::DxcCompiler SHARED IMPORTED)
    set_target_properties(VulkanSDK::DxcCompiler PROPERTIES
        IMPORTED_LOCATION "${VULKAN_SDK_LIB_DIR}/libdxcompiler.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_SDK_INCLUDE_DIR}"
    )

    target_link_libraries(utilities_host PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        VulkanSDK::Vulkan
        VulkanSDK::DxcCompiler
    )

    set(VULKAN_ICD_SOURCE "${VULKAN_SDK_ROOT}/macos/share/vulkan/icd.d/MoltenVK_icd.json")
    if(NOT EXISTS "${VULKAN_ICD_SOURCE}")
        message(FATAL_ERROR "MoltenVK ICD file not found at ${VULKAN_ICD_SOURCE}")
    endif()

    file(READ "${VULKAN_ICD_SOURCE}" VULKAN_ICD_CONTENT)
    string(REGEX REPLACE "\"[^\"]*/libMoltenVK\\.dylib\"" "\"./libMoltenVK.dylib\"" VULKAN_ICD_CONTENT "${VULKAN_ICD_CONTENT}")

    set(VULKAN_ICD_JSON "${CMAKE_BINARY_DIR}/MoltenVK_icd.json")
    file(WRITE "${VULKAN_ICD_JSON}" "${VULKAN_ICD_CONTENT}")
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES "${VULKAN_ICD_JSON}")
    target_compile_definitions(utilities_host PRIVATE UTILITIES_ICD_FILENAME=\"MoltenVK_icd.json\")

    set_target_properties(utilities_host PROPERTIES
        BUILD_RPATH "@loader_path"
    )

    add_custom_command(TARGET utilities_host POST_BUILD
        COMMAND install_name_tool -delete_rpath "${VULKAN_SDK_LIB_DIR}" "$<TARGET_FILE:utilities_host>" 2>/dev/null || true
    )

    set(VULKAN_HOST_RUNTIME_FILES
        "${VULKAN_SDK_LIB_DIR}/libvulkan.dylib"
        "${VULKAN_SDK_LIB_DIR}/libvulkan.1.dylib"
        "${VULKAN_SDK_LIB_DIR}/libMoltenVK.dylib"
        "${VULKAN_SDK_LIB_DIR}/libdxcompiler.dylib"
        "${VULKAN_ICD_JSON}"
    )

    foreach(vulkanBinary IN LISTS VULKAN_HOST_RUNTIME_FILES)
        add_custom_command(TARGET utilities_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${vulkanBinary}"
                "$<TARGET_FILE_DIR:utilities_host>"
        )
    endforeach()

    install(FILES ${VULKAN_HOST_RUNTIME_FILES} DESTINATION bin)
endif()

set_target_properties(utilities_host PROPERTIES OUTPUT_NAME "utilities_host")

add_library(utilities_app MODULE ${SRC_ROOT}/app.cpp)
target_include_directories(utilities_app PRIVATE ${SRC_ROOT} ${VULKAN_SDK_INCLUDE_DIR} ${VULKAN_VMA_INCLUDE_DIR} ${IMGUI_INCLUDE_DIR})

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