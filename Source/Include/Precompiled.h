#ifndef PRECOMPILED_H
#define PRECOMPILED_H

#ifdef _DEBUG
// Logging of VMA memory leaks in debug mode.
#define VMA_LEAK_LOG_FORMAT(format, ...) \
    do                                   \
    {                                    \
        printf((format), __VA_ARGS__);   \
        printf("\n");                    \
    }                                    \
    while (false)
#endif

/* clang-format off */
// NOTE: Preserve include order here (formatter wants to sort alphabetically.)
#include <volk.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
/* clang-format on */

#include <spdlog/sinks/ostream_sink.h> // For imgui.
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <intrin.h>

// Imgui Includes
// ---------------------------------------------------------

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

// GLM Includes
// ---------------------------------------------------------

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#endif
