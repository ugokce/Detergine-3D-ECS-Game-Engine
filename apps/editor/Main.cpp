#include "dg/core/Log.h"
#include "dg/runtime/Application.h"

// Editor host. The ImGui editor (DESIGN.md §13) arrives in roadmap phase 4; until then it runs the engine loop.
int main(int argc, char** argv) {
    dg::runtime::AppConfig config{.appName = "dg-editor"};
    auto outcome = dg::runtime::parseCommandLine(argc, argv, config);
    if (!outcome) {
        dg::log::error("dg-editor: {}", outcome.error());
        return 2;
    }
    if (*outcome == dg::runtime::ParseOutcome::ExitSuccess) {
        return 0;
    }
    dg::log::info("dg-editor: the editor UI is not implemented yet; running the engine loop (Ctrl+C to quit)");
    return dg::runtime::run(config);
}
