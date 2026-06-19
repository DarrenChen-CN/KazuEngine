// ============================================================================
// KazuEngine - Main Entry Point
// ============================================================================

#include "core/Utils.h"
#include "app/Application.h"
#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
    try {
        kazu::initLogger();
        const char* scenePath = (argc > 1) ? argv[1] : "assets/scenes/sample-scene.json";
        kazu::Application app;
        if (!app.init(scenePath)) return EXIT_FAILURE;
        app.run();
    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
