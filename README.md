# Detergine 3D ECS Game Engine

A natively deterministic, modular 3D/2D game engine: C++23 core, Vulkan renderer, Luau gameplay scripting, and
built-in lockstep/rollback multiplayer. Targets Windows x64, macOS ARM64, Android ARM64 and headless servers.

- **Engine manual** (HTML, from high-level architecture down to every system, with usage and editing guides): run
  `./dg docs --open`. Sources live in [docs/manual/](docs/manual/).
- Architecture and design decisions: [docs/DESIGN.md](docs/DESIGN.md)
- Contributor rules: [CLAUDE.md](CLAUDE.md)

## Quick start

Requirements: CMake 3.28+, Python 3.9+, Git, and a compiler:
- macOS: Xcode or the Xcode command line tools (Apple Silicon)
- Windows: Visual Studio 2022 or 2026 with the "C++ Clang tools for Windows" component
- Linux: Clang 19 or newer (older Clang cannot use libstdc++'s `std::expected`)
- macOS/Linux: `pkg-config`, which vcpkg needs (`brew install pkgconf` / `sudo apt install pkg-config`)
- Optional: Ninja for faster builds (`brew install ninja` / `winget install Ninja-build.Ninja`)

```bash
./dg setup --vcpkg    # check the toolchain and bootstrap the pinned vcpkg (once)
./dg run debug        # build + run the editor in Debug
./dg run release      # ... or in Release
./dg test             # build + run the GoogleTest suites, smoke and determinism tests
./dg hooks install    # optional: run the unit tests before every git push
./dg generate --ide   # create and open an Xcode / Visual Studio project
```

On Windows use `dg` instead of `./dg`. Run `./dg --help` for all commands.

### Launcher (GUI)

Prefer buttons? Run `./dg gui`, or double-click `Launcher.command` (macOS) / `Launcher.bat` (Windows). The launcher
exposes every preset, configuration, build option and run option, streams the build output, and shows the equivalent
`dg` command for each action. It uses only the Python standard library (tkinter). On Linux you may need
`sudo apt install python3-tk`; with Homebrew Python on macOS, `brew install python-tk`.
