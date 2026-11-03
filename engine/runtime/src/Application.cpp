#include "dg/runtime/Application.h"

#include "dg/core/BuildInfo.h"
#include "dg/core/Log.h"
#include "dg/sim/Simulation.h"

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

namespace dg::runtime {

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

extern "C" void onStopSignal(int) { gStopRequested = 1; }

template <typename T>
bool parseNumber(std::string_view text, T& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

void printUsage(std::string_view appName) {
    std::printf("Usage: %.*s [options]\n"
                "  --headless          No window, renderer or audio\n"
                "  --fast              Run ticks back to back instead of in real time\n"
                "  --ticks <n>         Stop after n ticks (default: run until Ctrl+C)\n"
                "  --tick-rate <hz>    Fixed simulation rate (default 30)\n"
                "  --players <n>       Number of scripted player slots (default 2)\n"
                "  --seed <n>          Simulation seed\n"
                "  --level <name>      Level to load\n"
                "  --version           Print build information\n"
                "  --help              Show this help\n",
                static_cast<int>(appName.size()), appName.data());
}

void printBuildInfo(std::string_view appName) {
    std::printf("%.*s %s (%s, %s-%s, %s%s)\n", static_cast<int>(appName.size()), appName.data(), build::kVersion,
                build::kConfig, build::kPlatform, build::kArch, build::kCompiler,
                build::kHeadless ? ", headless build" : "");
}

} // namespace

std::expected<ParseOutcome, std::string> parseCommandLine(int argc, char** argv, AppConfig& config) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::expected<std::string_view, std::string> {
            if (i + 1 >= argc) {
                return std::unexpected(std::format("{} expects a value", arg));
            }
            return std::string_view(argv[++i]);
        };
        auto number = [&]<typename T>(T& out) -> std::expected<void, std::string> {
            auto text = value();
            if (!text) {
                return std::unexpected(text.error());
            }
            if (!parseNumber(*text, out)) {
                return std::unexpected(std::format("{}: '{}' is not a valid number", arg, *text));
            }
            return {};
        };

        std::expected<void, std::string> result;
        if (arg == "--help" || arg == "-h") {
            printUsage(config.appName);
            return ParseOutcome::ExitSuccess;
        } else if (arg == "--version") {
            printBuildInfo(config.appName);
            return ParseOutcome::ExitSuccess;
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--fast") {
            config.fast = true;
        } else if (arg == "--ticks") {
            result = number(config.maxTicks);
        } else if (arg == "--tick-rate") {
            result = number(config.tickRate);
        } else if (arg == "--players") {
            result = number(config.players);
        } else if (arg == "--seed") {
            result = number(config.seed);
        } else if (arg == "--level") {
            auto text = value();
            if (!text) {
                return std::unexpected(text.error());
            }
            config.level = *text;
        } else {
            return std::unexpected(std::format("unknown argument '{}' (see --help)", arg));
        }

        if (!result) {
            return std::unexpected(result.error());
        }
    }

    if (config.tickRate == 0 || config.tickRate > 1000) {
        return std::unexpected("--tick-rate must be between 1 and 1000");
    }
    if (config.players == 0) {
        return std::unexpected("--players must be at least 1");
    }
    return ParseOutcome::Run;
}

int run(const AppConfig& config) {
    using Clock = std::chrono::steady_clock;

    log::info("{} {} ({}, {}-{}, {}) headless={} tick-rate={}Hz players={}", config.appName, build::kVersion,
              build::kConfig, build::kPlatform, build::kArch, build::kCompiler, config.headless, config.tickRate,
              config.players);
    if (!config.level.empty()) {
        log::warn("level loading is not implemented yet (roadmap phase 1); ignoring --level {}", config.level);
    }

    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);

    sim::Simulation simulation({.tickRate = config.tickRate, .seed = config.seed});
    std::vector<sim::InputCommand> inputs(config.players);

    const auto tickDuration = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(
        1.0 / static_cast<f64>(config.tickRate)));
    const auto start = Clock::now();
    const u64 reportInterval = static_cast<u64>(config.tickRate) * 5;

    while (gStopRequested == 0 && (config.maxTicks == 0 || simulation.tick() < config.maxTicks)) {
        if (!config.fast) {
            std::this_thread::sleep_until(start + tickDuration * static_cast<i64>(simulation.tick() + 1));
        }

        for (u32 slot = 0; slot < config.players; ++slot) {
            inputs[slot] = sim::scriptedInput(simulation.tick(), slot);
        }
        simulation.step(inputs);

        if (!config.fast && simulation.tick() % reportInterval == 0) {
            log::info("tick {} state-hash 0x{:016x}", simulation.tick(), simulation.stateHash());
        }
    }

    const f64 seconds = std::chrono::duration<f64>(Clock::now() - start).count();
    // Machine-readable line consumed by `dg determinism`; keep the format stable.
    log::info("final tick={} state-hash=0x{:016x} elapsed={:.3f}s", simulation.tick(), simulation.stateHash(),
              seconds);
    return 0;
}

int runMain(int argc, char** argv, AppConfig defaults) {
    auto outcome = parseCommandLine(argc, argv, defaults);
    if (!outcome) {
        log::error("{}: {}", defaults.appName, outcome.error());
        return 2;
    }
    if (*outcome == ParseOutcome::ExitSuccess) {
        return 0;
    }
    return run(defaults);
}

} // namespace dg::runtime
