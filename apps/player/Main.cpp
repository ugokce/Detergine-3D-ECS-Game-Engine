#include "dg/runtime/Application.h"

// Game runtime host. Renders and plays audio once those modules land; until then it runs the simulation loop.
int main(int argc, char** argv) {
    return dg::runtime::runMain(argc, argv, {.appName = "dg-player"});
}
