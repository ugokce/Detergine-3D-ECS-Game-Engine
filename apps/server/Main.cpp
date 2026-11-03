#include "dg/runtime/Application.h"

// Dedicated headless server (DESIGN.md §14.7, §15.1). Headless by default; no flag can turn presentation on.
int main(int argc, char** argv) {
    return dg::runtime::runMain(argc, argv, {.appName = "dg-server", .headless = true});
}
