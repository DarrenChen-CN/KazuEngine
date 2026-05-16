// ============================================================================
// KazuEngine - Main Entry Point
// ============================================================================

#include "core/Utils.h"
#include "app/Application.h"
#include <spdlog/spdlog.h>

int main() {
    try {
        kazu::initLogger();
        kazu::Application app;
        if (!app.init()) return EXIT_FAILURE;
        app.run();
    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
