#!/usr/bin/env python3
"""dg - Detergine engine developer tool.

One entry point to generate projects, build, run and test the engine on Windows, macOS and Linux.
Run through the launchers in the repository root: `./dg <command>` (macOS/Linux) or `dg <command>` (Windows).

  dg setup [--vcpkg]                 check the toolchain; optionally bootstrap vcpkg into external/vcpkg
  dg generate [--ide] [--preset P]   configure a build tree; --ide creates and opens an Xcode / Visual Studio project
  dg build [debug|dev|release] [app] build everything, or one app (editor, player, server, tests)
  dg run   [debug|dev|release] [app] [-- app args]   build, then run an app (default: editor where available)
  dg test  [debug|dev|release] [--label L]           build, then run ctest
  dg determinism                     run the player headless in Debug and Release and compare the final state hashes
  dg clean [--all] [--preset P]      delete a build tree (or all of build/)
  dg presets                         list the CMake presets available on this machine
  dg gui                             open the graphical launcher (all of the above, with every build option)
  dg hooks [install|uninstall]       git pre-push hook that runs `dg test dev -L unit`

Common options: --preset P, -D NAME=VALUE (e.g. -D DG_HEADLESS=ON), -j N, --verbose, --fresh.

Configurations: debug (no optimization), dev (optimized + asserts, the default), release (shipping optimization).
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

if sys.version_info < (3, 9):
    sys.exit("dg requires Python 3.9 or newer")

ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = ROOT / "build"
IS_WINDOWS = os.name == "nt"

# vcpkg release tag cloned by `dg setup --vcpkg`. Bumping it changes third-party package versions for everyone.
VCPKG_RELEASE = "2026.10.16"

CONFIGS = {"debug": "Debug", "dev": "Development", "development": "Development", "release": "Release"}
APPS = {"editor": "dg-editor", "player": "dg-player", "server": "dg-server", "tests": "dg-sim-tests"}


# ---------------------------------------------------------------------------------------------------------------------
# Helpers


def info(message: str) -> None:
    print(f"\033[1;36m[dg]\033[0m {message}" if sys.stdout.isatty() else f"[dg] {message}", flush=True)


def fail(message: str, code: int = 1) -> None:
    print(f"[dg] error: {message}", file=sys.stderr)
    sys.exit(code)


def run_cmd(cmd: list[str], env: dict[str, str] | None = None, check: bool = True) -> int:
    info("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd))
    code = subprocess.call(cmd, cwd=ROOT, env=env)
    if check and code != 0:
        fail(f"command failed with exit code {code}", code)
    return code


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def host_key() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Darwin":
        if machine not in ("arm64", "aarch64"):
            fail("macOS builds target Apple Silicon (arm64) only")
        return "macos"
    if system == "Windows":
        return "windows"
    if system == "Linux":
        return "linux"
    fail(f"unsupported host system '{system}'")
    return ""


def default_preset(ide: bool = False) -> str:
    host = host_key()
    if host == "macos":
        return "macos-arm64-xcode" if ide or not have("ninja") else "macos-arm64"
    if host == "windows":
        return "windows-x64-vs2022" if ide or not (have("ninja") and find_vs() is not None) else "windows-x64"
    if ide:
        fail("no IDE generator for Linux; use the default Ninja preset with your editor's CMake support")
    return "linux-x64"


def preset_dir(preset: str) -> Path:
    return BUILD_ROOT / preset


def existing_preset() -> str | None:
    """Prefer a build tree that already exists for this host, so `dg build` after `dg generate --ide` reuses it."""
    candidates = [default_preset(), default_preset(ide=True)] if host_key() != "linux" else [default_preset()]
    for name in candidates:
        if (preset_dir(name) / "CMakeCache.txt").exists():
            return name
    return None


def resolve_preset(explicit: str | None) -> str:
    return explicit or existing_preset() or default_preset()


def parse_words(words: list[str], allow_app: bool) -> tuple[str, str | None]:
    config, app = "Development", None
    for word in words:
        key = word.lower()
        if key in CONFIGS:
            config = CONFIGS[key]
        elif allow_app and key in APPS:
            app = key
        else:
            expected = "a configuration (debug, dev, release)" + (" or an app (editor, player, server, tests)"
                                                                     if allow_app else "")
            fail(f"'{word}' is not {expected}")
    return config, app


# ---------------------------------------------------------------------------------------------------------------------
# Windows: Ninja + clang-cl needs the Visual Studio developer environment


def find_vs() -> Path | None:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.exists():
        return None
    out = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                          "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                         capture_output=True, text=True)
    path = out.stdout.strip()
    return Path(path) if path else None


def build_env(preset: str) -> dict[str, str]:
    env = dict(os.environ)
    if not (IS_WINDOWS and preset == "windows-x64") or "VCToolsInstallDir" in env:
        return env
    vs = find_vs()
    if vs is None:
        fail("Visual Studio 2022 with C++ tools not found (needed for clang-cl); install it or use --ide")
    vcvars = vs / "VC/Auxiliary/Build/vcvars64.bat"
    out = subprocess.run(f'"{vcvars}" >nul && set', shell=True, capture_output=True, text=True)
    if out.returncode != 0:
        fail(f"failed to load the Visual Studio environment from {vcvars}")
    for line in out.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            env[key] = value
    return env


# ---------------------------------------------------------------------------------------------------------------------
# Commands


# Engine options a user may set with -D (shown in the launcher GUI; mirrors cmake/DgOptions.cmake).
ENGINE_OPTIONS = {
    "DG_BUILD_EDITOR": "Build the editor (desktop only)",
    "DG_BUILD_TESTS": "Build unit and determinism tests",
    "DG_HEADLESS": "Headless profile: no renderer, audio, UI or editor",
    "DG_WARNINGS_AS_ERRORS": "Treat compiler warnings as errors",
    "DG_ENABLE_ASAN": "AddressSanitizer",
    "DG_ENABLE_UBSAN": "UndefinedBehaviorSanitizer",
}

# Set from the command line in main(): cache definitions (-D), parallel jobs (-j), verbose build output, --fresh.
OPTIONS: dict = {"defines": {}, "jobs": 0, "verbose": False, "fresh": False}


def parse_defines(values: list[str] | None) -> dict[str, str]:
    defines = {}
    for item in values or []:
        name, sep, value = item.partition("=")
        if not sep or not name:
            fail(f"-D expects NAME=VALUE, got '{item}'")
        upper = value.upper()
        defines[name] = "ON" if upper in ("1", "ON", "TRUE", "YES") else "OFF" if upper in ("0", "OFF", "FALSE", "NO") \
            else value
    return defines


def read_cache(preset: str) -> dict[str, str]:
    cache = preset_dir(preset) / "CMakeCache.txt"
    values = {}
    if cache.exists():
        for line in cache.read_text(errors="replace").splitlines():
            match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
            if match:
                values[match.group(1)] = match.group(2)
    return values


def configure(preset: str, fresh: bool = False) -> None:
    cmd = ["cmake", "--preset", preset]
    if fresh or OPTIONS["fresh"]:
        cmd.append("--fresh")
    cmd += [f"-D{name}={value}" for name, value in OPTIONS["defines"].items()]
    run_cmd(cmd, env=build_env(preset))


def ensure_configured(preset: str) -> None:
    """Configures when there is no build tree yet, or when requested -D values differ from the cached ones."""
    cache = read_cache(preset)
    stale = [n for n, v in OPTIONS["defines"].items() if cache.get(n, "").upper() != v.upper()]
    if not cache or stale or OPTIONS["fresh"]:
        if cache and stale:
            info("options changed (" + ", ".join(stale) + "); reconfiguring")
        configure(preset)
        OPTIONS["fresh"] = False  # only once per invocation


def build(preset: str, config: str, target: str | None) -> None:
    ensure_configured(preset)
    cmd = ["cmake", "--build", str(preset_dir(preset)), "--config", config, "--parallel"]
    if OPTIONS["jobs"]:
        cmd.append(str(OPTIONS["jobs"]))
    if target:
        cmd += ["--target", target]
    if OPTIONS["verbose"]:
        cmd.append("--verbose")
    elif preset.endswith("-xcode"):
        cmd += ["--", "-quiet"]  # xcodebuild prints every build step otherwise
    run_cmd(cmd, env=build_env(preset))


def binary_path(preset: str, config: str, target: str) -> Path:
    suffix = ".exe" if IS_WINDOWS else ""
    return preset_dir(preset) / "bin" / config / f"{target}{suffix}"


def cmd_setup(args: argparse.Namespace) -> None:
    host = host_key()
    info(f"host: {platform.system()} {platform.machine()}  python {platform.python_version()}")
    checks = {"cmake": "CMake 3.28+", "git": "Git"}
    if host == "macos":
        checks.update({"ninja": "Ninja (optional; otherwise the Xcode generator is used)",
                       "xcodebuild": "Xcode command line tools",
                       "pkg-config": "pkg-config (required by vcpkg ports; brew install pkgconf)"})
    elif host == "windows":
        checks.update({"ninja": "Ninja (optional; otherwise Visual Studio is used)",
                       "clang-cl": "LLVM clang-cl (Visual Studio 'C++ Clang tools' component)"})
    else:
        checks.update({"ninja": "Ninja", "clang++": "Clang", "pkg-config": "pkg-config (required by vcpkg ports)"})

    missing = []
    for tool, description in checks.items():
        found = shutil.which(tool)
        print(f"  {'ok     ' if found else 'MISSING'}  {tool:<10} {description}{'' if not found else '  ' + found}")
        if not found:
            missing.append(tool)

    if have("cmake"):
        version = subprocess.run(["cmake", "--version"], capture_output=True, text=True).stdout.split()[2]
        major, minor = (int(p) for p in version.split(".")[:2])
        if (major, minor) < (3, 28):
            fail(f"CMake {version} is too old; 3.28 or newer is required")

    vcpkg = os.environ.get("VCPKG_ROOT") or (str(ROOT / "external/vcpkg") if (ROOT / "external/vcpkg").exists()
                                             else None)
    print(f"  {'ok     ' if vcpkg else 'MISSING'}  {'vcpkg':<10} package manager{'  ' + vcpkg if vcpkg else ''}")
    if "ninja" in missing:
        hint = {"macos": "brew install ninja", "windows": "winget install Ninja-build.Ninja",
                "linux": "sudo apt install ninja-build"}[host]
        info(f"install Ninja for faster builds: {hint}")

    if args.vcpkg and not vcpkg:
        target = ROOT / "external/vcpkg"
        if not args.yes:
            answer = input(f"Clone https://github.com/microsoft/vcpkg into {target} and bootstrap it? [y/N] ")
            if answer.strip().lower() not in ("y", "yes"):
                fail("vcpkg setup cancelled")
        # Pinned release so every machine and CI runner resolves identical package versions.
        run_cmd(["git", "clone", "--depth", "1", "--branch", VCPKG_RELEASE, "https://github.com/microsoft/vcpkg.git",
                 str(target)])
        script = target / ("bootstrap-vcpkg.bat" if IS_WINDOWS else "bootstrap-vcpkg.sh")
        run_cmd([str(script), "-disableMetrics"])
        info("vcpkg ready; re-run `dg generate` so CMake picks it up")

    required = [t for t in missing if t in ("cmake", "git", "xcodebuild", "clang++", "pkg-config")]
    if required:
        fail("missing required tools: " + ", ".join(required))
    info("toolchain OK")


def cmd_generate(args: argparse.Namespace) -> None:
    preset = args.preset or default_preset(ide=args.ide)
    configure(preset, fresh=args.fresh)
    if args.ide:
        projects = sorted(preset_dir(preset).glob("*.xcodeproj")) + sorted(preset_dir(preset).glob("*.sln"))
        if not projects:
            fail("no IDE project was generated")
        info(f"opening {projects[0]}")
        if IS_WINDOWS:
            os.startfile(projects[0])  # type: ignore[attr-defined]
        elif host_key() == "macos":
            subprocess.call(["open", str(projects[0])])


def cmd_build(args: argparse.Namespace) -> None:
    config, app = parse_words(args.words, allow_app=True)
    preset = resolve_preset(args.preset)
    build(preset, config, APPS[app] if app else None)
    info(f"built {app or 'everything'} ({config}) in {preset_dir(preset)}")


def cmd_run(args: argparse.Namespace) -> None:
    config, app = parse_words(args.words, allow_app=True)
    preset = resolve_preset(args.preset)
    app = app or ("player" if preset.startswith("android") else "editor")
    if app == "editor" and preset.endswith("-server"):
        app = "server"
    target = APPS[app]
    build(preset, config, target)
    exe = binary_path(preset, config, target)
    if not exe.exists():
        fail(f"{exe} was not produced (is '{app}' built for preset {preset}?)")
    code = run_cmd([str(exe), *args.app_args], check=False)
    sys.exit(code)


def cmd_test(args: argparse.Namespace) -> None:
    config, _ = parse_words(args.words, allow_app=False)
    preset = resolve_preset(args.preset)
    build(preset, config, None)
    cmd = ["ctest", "--test-dir", str(preset_dir(preset)), "-C", config, "--output-on-failure"]
    if args.label:
        cmd += ["-L", args.label]
    run_cmd(cmd, env=build_env(preset))


def cmd_determinism(args: argparse.Namespace) -> None:
    preset = resolve_preset(args.preset)
    hashes = {}
    for config in ("Debug", "Release"):
        build(preset, config, "dg-player")
        exe = binary_path(preset, config, "dg-player")
        out = subprocess.run([str(exe), "--headless", "--fast", "--ticks", str(args.ticks)],
                             capture_output=True, text=True)
        match = re.search(r"state-hash=(0x[0-9a-f]+)", out.stdout)
        if out.returncode != 0 or not match:
            fail(f"{config} run failed:\n{out.stdout}{out.stderr}")
        hashes[config] = match.group(1)
        info(f"{config:<8} {hashes[config]}")
    if len(set(hashes.values())) != 1:
        fail("DESYNC: Debug and Release produced different simulation states")
    info(f"Debug and Release are bit-identical after {args.ticks} ticks")


def cmd_clean(args: argparse.Namespace) -> None:
    target = BUILD_ROOT if args.all else preset_dir(resolve_preset(args.preset))
    if target.exists():
        info(f"removing {target}")
        shutil.rmtree(target)
    else:
        info(f"nothing to clean at {target}")


def host_presets() -> list[dict]:
    """Configure presets from CMakePresets.json that can be used on this host, with their effective settings."""
    data = json.loads((ROOT / "CMakePresets.json").read_text())
    by_name = {p["name"]: p for p in data["configurePresets"]}
    system = platform.system()

    def resolved(preset: dict) -> dict:
        merged: dict = {"cacheVariables": {}}
        parents = preset.get("inherits", [])
        for parent in [parents] if isinstance(parents, str) else parents:
            base = resolved(by_name[parent])
            merged["cacheVariables"].update(base["cacheVariables"])
            for key in ("condition", "generator"):
                if merged.get(key) is None and base.get(key) is not None:
                    merged[key] = base[key]
        for key in ("condition", "generator"):
            if preset.get(key) is not None:
                merged[key] = preset[key]
        merged["cacheVariables"].update(preset.get("cacheVariables", {}))
        return merged

    result = []
    for preset in data["configurePresets"]:
        if preset.get("hidden"):
            continue
        effective = resolved(preset)
        condition = effective.get("condition")
        if condition and condition.get("rhs") != system:
            continue
        result.append({"name": preset["name"], "displayName": preset.get("displayName", preset["name"]),
                       "generator": effective.get("generator", ""), "cacheVariables": effective["cacheVariables"],
                       "configured": (preset_dir(preset["name"]) / "CMakeCache.txt").exists()})
    return result


def cmd_gui(_: argparse.Namespace) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        import dg_launcher  # noqa: PLC0415 - tkinter is only needed for the GUI
    except ImportError as error:
        fail(f"the launcher needs tkinter ({error}); install it, e.g. `brew install python-tk` or "
             "`sudo apt install python3-tk`")
    dg_launcher.main()


HOOK_MARKER = "# installed by `dg hooks install`"
PRE_PUSH_HOOK = f"""#!/bin/sh
{HOOK_MARKER}
# Runs the fast unit and determinism tests before anything leaves this machine. Bypass once with `git push --no-verify`.
cd "$(git rev-parse --show-toplevel)" || exit 1
exec python3 tools/dg.py test dev -L unit
"""


def cmd_hooks(args: argparse.Namespace) -> None:
    hooks = Path(subprocess.run(["git", "rev-parse", "--git-path", "hooks"], cwd=ROOT, capture_output=True,
                                text=True, check=True).stdout.strip())
    hook = (ROOT / hooks if not hooks.is_absolute() else hooks) / "pre-push"
    ours = hook.exists() and HOOK_MARKER in hook.read_text(errors="replace")
    if args.action == "install":
        if hook.exists() and not ours:
            fail(f"{hook} already exists and was not created by dg; merge it by hand")
        hook.parent.mkdir(parents=True, exist_ok=True)
        hook.write_text(PRE_PUSH_HOOK)
        hook.chmod(0o755)
        info(f"installed pre-push hook: {hook} (runs `dg test dev -L unit`)")
    elif args.action == "uninstall":
        if ours:
            hook.unlink()
            info(f"removed {hook}")
        else:
            info("no dg pre-push hook installed")
    else:
        info(f"pre-push hook: {'installed' if ours else 'not installed'} ({hook})")


def cmd_presets(_: argparse.Namespace) -> None:
    run_cmd(["cmake", "--list-presets=all"])
    info(f"default preset on this machine: {default_preset()}  (IDE: "
         f"{default_preset(ide=True) if host_key() != 'linux' else 'n/a'})")


# ---------------------------------------------------------------------------------------------------------------------


def main() -> None:
    argv = sys.argv[1:]
    app_args: list[str] = []
    if "--" in argv:
        split = argv.index("--")
        argv, app_args = argv[:split], argv[split + 1:]

    parser = argparse.ArgumentParser(prog="dg", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name: str, func, words: bool = False, help_text: str = ""):
        p = sub.add_parser(name, help=help_text)
        p.set_defaults(func=func)
        if words:
            p.add_argument("words", nargs="*", help="configuration and/or app, in any order")
        p.add_argument("--preset", help="CMake configure preset (default: chosen for this machine)")
        p.add_argument("-D", dest="defines", action="append", metavar="NAME=VALUE",
                       help="set a CMake cache option, e.g. -D DG_HEADLESS=ON (reconfigures when it changes)")
        p.add_argument("-j", "--jobs", type=int, default=0, help="parallel build jobs (default: all cores)")
        p.add_argument("--verbose", action="store_true", help="show full compiler command lines")
        return p

    p = add("setup", cmd_setup, help_text="check the toolchain")
    p.add_argument("--vcpkg", action="store_true", help="clone and bootstrap vcpkg into external/vcpkg")
    p.add_argument("--yes", "-y", action="store_true", help="do not ask for confirmation")
    p = add("generate", cmd_generate, help_text="configure a build tree / IDE project")
    p.add_argument("--ide", action="store_true", help="generate and open an Xcode or Visual Studio project")
    add("build", cmd_build, words=True, help_text="build")
    add("run", cmd_run, words=True, help_text="build and run an app")
    p = add("test", cmd_test, words=True, help_text="build and run tests")
    p.add_argument("--label", "-L", help="only run tests with this ctest label (e.g. determinism, smoke)")
    p = add("determinism", cmd_determinism, help_text="compare Debug and Release sim results")
    p.add_argument("--ticks", type=int, default=1800)
    p = add("clean", cmd_clean, help_text="delete build trees")
    p.add_argument("--all", action="store_true", help="delete the whole build/ directory")
    add("presets", cmd_presets, help_text="list presets")
    add("gui", cmd_gui, help_text="open the graphical launcher")
    p = add("hooks", cmd_hooks, help_text="install a git pre-push hook that runs the unit tests")
    p.add_argument("action", choices=["install", "uninstall", "status"], nargs="?", default="status")
    for p in sub.choices.values():
        p.add_argument("--fresh", action="store_true", help="discard the CMake cache and reconfigure")

    args = parser.parse_args(argv)
    args.app_args = app_args
    OPTIONS.update(defines=parse_defines(args.defines), jobs=args.jobs, verbose=args.verbose, fresh=args.fresh)
    if not have("cmake"):
        fail("CMake not found on PATH (3.28+ required)")
    args.func(args)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
