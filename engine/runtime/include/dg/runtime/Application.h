#pragma once

#include "dg/core/Log.h"
#include "dg/core/Types.h"

#include <expected>
#include <string>
#include <string_view>

namespace dg::runtime {

struct AppConfig {
    std::string_view appName = "dg";
    bool headless = false;
    bool fast = false;    // run ticks back to back instead of in real time (tests, replays, determinism checks)
    u64 maxTicks = 0;     // 0 = run until interrupted
    u32 tickRate = 30;
    u32 players = 2;
    u64 seed = 0x5eedu;
    std::string level{};  // level to load (headless servers are level-scoped, DESIGN.md §15.1)
    log::Level logLevel = log::Level::Info;
    std::string logFile{}; // optional log file in addition to the console
};

enum class ParseOutcome { Run, ExitSuccess };

// Parses the shared engine command line into `config`. Returns an error message for invalid arguments.
std::expected<ParseOutcome, std::string> parseCommandLine(int argc, char** argv, AppConfig& config);

// Starts asynchronous engine logging with the app's --log-level / --log-file settings. Fails (after logging why) if
// --log-file cannot be opened; apps then exit with code 2, as for any other invalid argument.
std::expected<void, std::string> startLogging(const AppConfig& config);

// Runs the fixed-tick main loop. Returns the process exit code.
int run(const AppConfig& config);

// parseCommandLine + startLogging + run + log::shutdown with the given defaults: the whole body of an app's main().
int runMain(int argc, char** argv, AppConfig defaults);

} // namespace dg::runtime
