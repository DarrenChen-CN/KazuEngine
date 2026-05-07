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
)

target_include_directories(KazuEngine PRIVATE
    ${vma_SOURCE_DIR}/include
    ${spirv-reflect_SOURCE_DIR}
    ${spirv-reflect_SOURCE_DIR}/include
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

# Copy texture to build dir
add_custom_command(TARGET KazuEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "D:/code/projects/LearnOpenGL/image/container.png" $<TARGET_FILE_DIR:KazuEngine>/container.png
)
