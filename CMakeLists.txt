cmake_minimum_required(VERSION 3.20)
project(GPUConfig LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "GPUConfig is x64-only. Re-configure with an x64 generator.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "GPUConfig targets MSVC on Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

add_compile_options(/W4 /permissive- /Zc:__cplusplus)

set(GPUCONFIG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GPUCONFIG_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GPUCONFIG_OUTPUT_DIR}")

foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${GPUCONFIG_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${GPUCONFIG_OUTPUT_DIR}")
endforeach()

add_subdirectory(third_party/minhook)

set(GPUCONFIG_SOURCES
    src/dllmain.cpp
    src/log.cpp
    src/config.cpp
    src/dxgi_hooks.cpp
    src/vulkan_hooks.cpp
    src/opengl_hooks.cpp
    src/identity_hooks.cpp
    src/wmi_hooks.cpp
    src/system_hooks.cpp
    src/cpuid_hooks.cpp
    src/version_exports.cpp
)

add_library(version SHARED ${GPUCONFIG_SOURCES})
set_target_properties(version PROPERTIES
    OUTPUT_NAME "gpuconfig_proxy"
    PREFIX ""
)
target_link_libraries(version PRIVATE minhook)
target_link_libraries(version PRIVATE shlwapi dxgi opengl32 setupapi ole32 oleaut32 wbemuuid)
target_include_directories(version PRIVATE src third_party/vulkan-headers/include third_party/minhook/src)

add_custom_command(TARGET version POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:version>"
        "${GPUCONFIG_OUTPUT_DIR}/version.dll"
    COMMENT "Renaming gpuconfig_proxy.dll -> version.dll for deployment"
)

add_custom_target(copy_ini ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/GPUConfig.ini"
        "${GPUCONFIG_OUTPUT_DIR}/GPUConfig.ini"
    DEPENDS "${CMAKE_SOURCE_DIR}/GPUConfig.ini"
)
