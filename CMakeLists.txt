cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/External/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

set(PROJECT_NAME Vulkan-Raytracing-Shader-Objects)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Packages
# --------------------------------

find_package(spdlog                REQUIRED)
find_package(volk                  REQUIRED)
find_package(glfw3                 REQUIRED)
find_package(glm                   REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(tinyobjloader         REQUIRED)

# Imgui
# --------------------------------

set(IMGUI_INC ${CMAKE_SOURCE_DIR}/External/imgui/
              ${CMAKE_SOURCE_DIR}/External/imgui/backends/)

file(GLOB IMGUI_SRC ${CMAKE_SOURCE_DIR}/External/imgui/imgui.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_widgets.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_tables.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_draw.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_demo.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/backends/imgui_impl_glfw.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/backends/imgui_impl_vulkan.cpp)
        
# Executable
# --------------------------------

add_executable(${PROJECT_NAME} 
    Source/Main.cpp
    Source/Precompiled.cpp
    Source/Common.cpp
    Source/RenderContext.cpp
    ${IMGUI_SRC}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE Source/Include/Precompiled.h)

# Include
# --------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE
    Source/Include
    ${IMGUI_INC}
)

# Link
# --------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE 
    volk::volk_headers 
    spdlog::spdlog_header_only
    glfw
    GPUOpen::VulkanMemoryAllocator
    tinyobjloader::tinyobjloader
)

# Defines
# --------------------------------

target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)