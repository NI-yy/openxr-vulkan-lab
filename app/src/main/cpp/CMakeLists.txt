cmake_minimum_required(VERSION 3.22.1)
project(openxr_vulkan_lab LANGUAGES C CXX)

include(FetchContent)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(openxrsdk
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK-Source.git
    GIT_TAG 3ed64d0f9bb680f24b80a085091e5c8fab38f7b7
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(openxrsdk)

find_library(android-lib android REQUIRED)
find_library(log-lib log REQUIRED)
find_library(vulkan-lib vulkan REQUIRED)

set(native_glue "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(openxr_vulkan_lab SHARED
    main.cpp
    renderer.cpp
    scene.cpp
    "${native_glue}/android_native_app_glue.c"
)
target_include_directories(openxr_vulkan_lab PRIVATE "${native_glue}")
target_compile_features(openxr_vulkan_lab PRIVATE cxx_std_17)
target_compile_definitions(openxr_vulkan_lab PRIVATE XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_VULKAN)
target_link_libraries(openxr_vulkan_lab PRIVATE OpenXR::openxr_loader ${android-lib} ${log-lib} ${vulkan-lib})
