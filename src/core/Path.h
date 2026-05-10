// ============================================================================
// KazuEngine - Path Utilities
//
// Resolves paths relative to the project root or executable location.
// When the working directory differs from the project root (e.g. running
// from build/Debug/), relative paths like "shaders/triangle.vert.spv"
// would fail. This utility searches candidate directories to find the
// correct file.
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kazu {

namespace Path {

inline std::string getExeDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return (std::filesystem::path(buffer).parent_path() / "").string();
#else
    return "./";
#endif
}

inline std::string getProjectRoot() {
    static std::string root;
    if (!root.empty()) return root;

#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(buffer).parent_path();
    while (!dir.empty() && dir != dir.parent_path()) {
        if (std::filesystem::exists(dir / "CMakeLists.txt") ||
            std::filesystem::exists(dir / ".git")) {
            root = (dir / "").string();
            return root;
        }
        dir = dir.parent_path();
    }
#endif
    root = (std::filesystem::current_path() / "").string();
    return root;
}

// Resolve a shader file by searching candidate directories in order:
//   1. exeDir/shaders/
//   2. projectRoot/shaders/
//   3. projectRoot/build/shaders/
inline std::string resolveShader(const std::string& filename) {
    std::string exeDir = getExeDirectory();
    std::string projRoot = getProjectRoot();

    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(exeDir) / "shaders" / filename,
        std::filesystem::path(projRoot) / "shaders" / filename,
        std::filesystem::path(projRoot) / "build" / "shaders" / filename,
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }
    // Fallback: return first candidate (produces clear error later if missing)
    return candidates[0].string();
}

// Resolve a generic path relative to project root
inline std::string resolve(const std::string& relativePath) {
    return (std::filesystem::path(getProjectRoot()) / relativePath).string();
}

} // namespace Path

} // namespace kazu
