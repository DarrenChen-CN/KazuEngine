cmake_minimum_required(VERSION 3.20)
project(KazuEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Dependencies fetched via FetchContent: source goes to external/, build artifacts to build/
include(FetchContent)

# GLFW
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.3.8
        SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/glfw
        BINARY_DIR     ${CMAKE_BINARY_DIR}/glfw-build
        SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/glfw-subbuild
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# spdlog (header-only, submodule)
add_subdirectory(external/spdlog)

# VMA (Vulkan Memory Allocator)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDks/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/vma
    BINARY_DIR     ${CMAKE_BINARY_DIR}/vma-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/vma-subbuild
)
FetchContent_MakeAvailable(vma)

# glm (header-only math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/glm-src
    BINARY_DIR     ${CMAKE_BINARY_DIR}/glm-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/glm-subbuild
)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()

# nlohmann/json (JSON parser, header-only)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/json
    BINARY_DIR     ${CMAKE_BINARY_DIR}/json-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/json-subbuild
)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
endif()

# fastgltf (GLTF/GLB loader)
FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        main
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/fastgltf
    BINARY_DIR     ${CMAKE_BINARY_DIR}/fastgltf-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/fastgltf-subbuild
)
FetchContent_MakeAvailable(fastgltf)

# SPIRV-Reflect (for shader reflection)
FetchContent_Declare(
    spirv-reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG        vulkan-sdk-1.3.275.0
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/spirv-reflect
    BINARY_DIR     ${CMAKE_BINARY_DIR}/spirv-reflect-build
    SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/spirv-reflect-subbuild
)
FetchContent_GetProperties(spirv-reflect)
if(NOT spirv-reflect_POPULATED)
    FetchContent_Populate(spirv-reflect)
endif()

# Executable
file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(KazuEngine ${SOURCES})

target_link_libraries(KazuEngine PRIVATE
    Vulkan::Vulkan
    glfw
    spdlog::spdlog
    fastgltf
)

target_include_directories(KazuEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${vma_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${json_SOURCE_DIR}/single_include
    ${spirv-reflect_SOURCE_DIR}
    ${spirv-reflect_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/imgui
    ${CMAKE_SOURCE_DIR}/external/imgui/backends
)

# ImGui (submodule)
target_sources(KazuEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_vulkan.cpp
)

# SPIRV-Reflect C implementation
target_sources(KazuEngine PRIVATE ${spirv-reflect_SOURCE_DIR}/spirv_reflect.c)

# Shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator 
    PATHS 
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Please install Vulkan SDK.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADERS
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
    ${SHADER_SOURCE_DIR}/gbuffer.frag
    ${SHADER_SOURCE_DIR}/lighting.vert
    ${SHADER_SOURCE_DIR}/lighting.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(FILENAME ${SHADER} NAME)
    set(SPIRV "${SHADER_BINARY_DIR}/${FILENAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${FILENAME}"
    )
    list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_FILES})
add_dependencies(KazuEngine Shaders)

# Copy shaders to build dir for debugging
add_custom_command(TARGET KazuEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SHADER_BINARY_DIR} $<TARGET_FILE_DIR:KazuEngine>/shaders
)

# Copy assets to build dir
add_custom_command(TARGET KazuEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:KazuEngine>/assets
)
