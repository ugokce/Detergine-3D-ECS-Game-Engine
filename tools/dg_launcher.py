#!/usr/bin/env python3
"""Detergine Launcher - a small GUI over the `dg` developer tool.

Pick a platform preset, configuration, target and build options, then generate, build, run and test the engine with
one click. Every action runs the equivalent `dg` command (shown at the bottom, ready to copy), so the GUI and the
command line always behave the same.

Start it with `./dg gui` (macOS/Linux), `dg gui` (Windows), or `python3 tools/dg_launcher.py`.
Uses only the Python standard library (tkinter).
"""

from __future__ import annotations

import json
import os
import queue
import shlex
import signal
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dg  # noqa: E402

SETTINGS_FILE = dg.ROOT / ".dg" / "launcher.json"
CONFIGS = [("Debug", "debug"), ("Development", "dev"), ("Release", "release")]
TARGETS = ["all", "editor", "player", "server", "tests"]
RUN_APPS = ["editor", "player", "server"]
TEST_LABELS = ["all", "determinism", "smoke"]
OPTION_DEFAULTS = {"DG_BUILD_EDITOR": True, "DG_BUILD_TESTS": True, "DG_HEADLESS": False,
                   "DG_WARNINGS_AS_ERRORS": False,
                   "DG_ENABLE_ASAN": False, "DG_ENABLE_UBSAN": False}

# Harmless xcodebuild chatter on machines whose iOS simulator runtime is out of date.
NOISE = ("CoreSimulator", "DVTErrorPresenter", "Domain: DVTCoreSimulator", "iOSSimulator:", "Recovery Suggestion: Please ensure",
         "Simulator device support disabled")


class Launcher(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Detergine Launcher")
        self.minsize(980, 680)
        self.process: subprocess.Popen | None = None
        self.output: queue.Queue[str | None] = queue.Queue()
        self.presets = dg.host_presets()
        self.preset_by_label = {f"{p['displayName']}  [{p['name']}]": p for p in self.presets}
        self.action_buttons: list[ttk.Button] = []

        settings = self.load_settings()
        default_preset = settings.get("preset") or dg.existing_preset() or dg.default_preset()
        self.preset_var = tk.StringVar(value=self.label_for(default_preset))
        self.config_var = tk.StringVar(value=settings.get("config", "Development"))
        self.target_var = tk.StringVar(value=settings.get("target", "all"))
        self.option_vars = {name: tk.BooleanVar(value=settings.get("options", {}).get(name, default))
                            for name, default in OPTION_DEFAULTS.items()}
        self.jobs_var = tk.IntVar(value=settings.get("jobs", 0))
        self.verbose_var = tk.BooleanVar(value=settings.get("verbose", False))
        self.fresh_var = tk.BooleanVar(value=False)
        self.app_var = tk.StringVar(value=settings.get("app", "editor"))
        self.run_headless_var = tk.BooleanVar(value=settings.get("run_headless", False))
        self.run_fast_var = tk.BooleanVar(value=settings.get("run_fast", False))
        self.ticks_var = tk.StringVar(value=settings.get("ticks", ""))
        self.tick_rate_var = tk.StringVar(value=settings.get("tick_rate", "30"))
        self.players_var = tk.StringVar(value=settings.get("players", "2"))
        self.seed_var = tk.StringVar(value=settings.get("seed", ""))
        self.level_var = tk.StringVar(value=settings.get("level", ""))
        self.extra_args_var = tk.StringVar(value=settings.get("extra_args", ""))
        self.test_label_var = tk.StringVar(value=settings.get("test_label", "all"))
        self.hide_noise_var = tk.BooleanVar(value=settings.get("hide_noise", True))
        self.status_var = tk.StringVar(value="Ready")
        self.command_var = tk.StringVar()

        self.build_ui()
        for var in (self.preset_var, self.config_var, self.target_var, self.jobs_var, self.verbose_var, self.fresh_var,
                    self.app_var, self.run_headless_var, self.run_fast_var, self.ticks_var, self.tick_rate_var,
                    self.players_var, self.seed_var, self.level_var, self.extra_args_var, self.test_label_var,
                    *self.option_vars.values()):
            var.trace_add("write", lambda *_: self.on_change())
        self.on_change()
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(50, self.pump_output)

    # -- layout -------------------------------------------------------------------------------------------------------

    def build_ui(self) -> None:
        style = ttk.Style(self)
        if "clam" in style.theme_names() and sys.platform.startswith("linux"):
            style.theme_use("clam")
        style.configure("Primary.TButton", font=("TkDefaultFont", 11, "bold"))

        root = ttk.Frame(self, padding=10)
        root.pack(fill="both", expand=True)
        root.columnconfigure(1, weight=1)
        root.rowconfigure(0, weight=1)

        left = ttk.Frame(root)
        left.grid(row=0, column=0, sticky="ns", padx=(0, 10))
        self.build_project_group(left)
        self.build_options_group(left)
        self.build_run_group(left)
        self.build_test_group(left)

        right = ttk.Frame(root)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(1, weight=1)
        self.build_actions(right)
        self.build_console(right)

        bottom = ttk.Frame(root)
        bottom.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        bottom.columnconfigure(1, weight=1)
        ttk.Label(bottom, text="Command:").grid(row=0, column=0, sticky="w")
        command = ttk.Entry(bottom, textvariable=self.command_var, state="readonly", font=("TkFixedFont", 10))
        command.grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(bottom, text="Copy", width=6, command=self.copy_command).grid(row=0, column=2)
        ttk.Label(bottom, textvariable=self.status_var, foreground="#666").grid(row=1, column=0, columnspan=3,
                                                                              sticky="w", pady=(4, 0))

    def group(self, parent: ttk.Frame, title: str) -> ttk.LabelFrame:
        frame = ttk.LabelFrame(parent, text=title, padding=8)
        frame.pack(fill="x", pady=(0, 8))
        frame.columnconfigure(1, weight=1)
        return frame

    def build_project_group(self, parent: ttk.Frame) -> None:
        g = self.group(parent, "Project")
        ttk.Label(g, text="Platform preset").grid(row=0, column=0, sticky="w")
        ttk.Combobox(g, textvariable=self.preset_var, values=list(self.preset_by_label), state="readonly",
                     width=46).grid(row=0, column=1, sticky="ew", pady=2)
        ttk.Label(g, text="Configuration").grid(row=1, column=0, sticky="w")
        configs = ttk.Frame(g)
        configs.grid(row=1, column=1, sticky="w", pady=2)
        for name, _ in CONFIGS:
            ttk.Radiobutton(configs, text=name, value=name, variable=self.config_var).pack(side="left", padx=(0, 10))
        ttk.Label(g, text="Build target").grid(row=2, column=0, sticky="w")
        ttk.Combobox(g, textvariable=self.target_var, values=TARGETS, state="readonly", width=12).grid(
            row=2, column=1, sticky="w", pady=2)
        self.preset_note = ttk.Label(g, text="", foreground="#a60", wraplength=380)
        self.preset_note.grid(row=3, column=0, columnspan=2, sticky="w")

    def build_options_group(self, parent: ttk.Frame) -> None:
        g = self.group(parent, "Build options (CMake)")
        for row, (name, description) in enumerate(dg.ENGINE_OPTIONS.items()):
            ttk.Checkbutton(g, text=f"{description}", variable=self.option_vars[name]).grid(
                row=row, column=0, columnspan=2, sticky="w")
        row = len(dg.ENGINE_OPTIONS)
        jobs = ttk.Frame(g)
        jobs.grid(row=row, column=0, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Label(jobs, text="Parallel jobs (0 = all cores)").pack(side="left")
        ttk.Spinbox(jobs, from_=0, to=256, textvariable=self.jobs_var, width=5).pack(side="left", padx=6)
        ttk.Checkbutton(g, text="Verbose build output", variable=self.verbose_var).grid(
            row=row + 1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(g, text="Fresh configure (discard CMake cache) on next action",
                        variable=self.fresh_var).grid(row=row + 2, column=0, columnspan=2, sticky="w")

    def build_run_group(self, parent: ttk.Frame) -> None:
        g = self.group(parent, "Run options")
        ttk.Label(g, text="App").grid(row=0, column=0, sticky="w")
        self.app_combo = ttk.Combobox(g, textvariable=self.app_var, values=RUN_APPS, state="readonly", width=12)
        self.app_combo.grid(row=0, column=1, sticky="w", pady=2)
        flags = ttk.Frame(g)
        flags.grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(flags, text="Headless", variable=self.run_headless_var).pack(side="left", padx=(0, 10))
        ttk.Checkbutton(flags, text="Fast (no real-time pacing)", variable=self.run_fast_var).pack(side="left")
        fields = [("Ticks (empty = until stopped)", self.ticks_var), ("Tick rate (Hz)", self.tick_rate_var),
                  ("Players", self.players_var), ("Seed (empty = default)", self.seed_var),
                  ("Level", self.level_var), ("Extra arguments", self.extra_args_var)]
        for row, (label, var) in enumerate(fields, start=2):
            ttk.Label(g, text=label).grid(row=row, column=0, sticky="w")
            ttk.Entry(g, textvariable=var, width=24).grid(row=row, column=1, sticky="ew", pady=1)

    def build_test_group(self, parent: ttk.Frame) -> None:
        g = self.group(parent, "Tests")
        ttk.Label(g, text="Label filter").grid(row=0, column=0, sticky="w")
        ttk.Combobox(g, textvariable=self.test_label_var, values=TEST_LABELS, state="readonly", width=12).grid(
            row=0, column=1, sticky="w")

    def build_actions(self, parent: ttk.Frame) -> None:
        bar = ttk.Frame(parent)
        bar.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        # `dg run` always builds first, so "Run" is build-and-run.
        primary = [("Build", self.do_build), ("Run", self.do_run), ("Test", self.do_test)]
        secondary = [("Check Setup", self.do_setup), ("Generate", self.do_generate), ("Open IDE", self.do_ide),
                     ("Determinism", self.do_determinism), ("Clean", self.do_clean)]
        row1, row2 = ttk.Frame(bar), ttk.Frame(bar)
        row1.pack(fill="x")
        row2.pack(fill="x", pady=(4, 0))
        for text, command in primary:
            button = ttk.Button(row1, text=text, command=command, style="Primary.TButton", width=10)
            button.pack(side="left", padx=(0, 6))
            self.action_buttons.append(button)
        self.stop_button = ttk.Button(row1, text="Stop", command=self.stop, state="disabled", width=8)
        self.stop_button.pack(side="right")
        for text, command in secondary:
            button = ttk.Button(row2, text=text, command=command)
            button.pack(side="left", padx=(0, 6))
            self.action_buttons.append(button)
        ttk.Button(row2, text="Open Build Folder", command=self.open_build_folder).pack(side="right")

    def build_console(self, parent: ttk.Frame) -> None:
        frame = ttk.Frame(parent)
        frame.grid(row=1, column=0, sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)
        self.console = tk.Text(frame, wrap="word", font=("TkFixedFont", 11), bg="#1e1f22", fg="#d4d4d4",
                               insertbackground="#d4d4d4", relief="flat", padx=8, pady=6, state="disabled")
        self.console.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(frame, command=self.console.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.console.configure(yscrollcommand=scroll.set)
        self.console.tag_configure("dg", foreground="#56b6c2")
        self.console.tag_configure("error", foreground="#f07178")
        self.console.tag_configure("warn", foreground="#e5c07b")
        self.console.tag_configure("ok", foreground="#98c379")
        tools = ttk.Frame(parent)
        tools.grid(row=2, column=0, sticky="ew", pady=(4, 0))
        ttk.Checkbutton(tools, text="Hide Xcode simulator noise", variable=self.hide_noise_var).pack(side="left")
        ttk.Button(tools, text="Clear Output", command=self.clear_console).pack(side="right")

    # -- state --------------------------------------------------------------------------------------------------------

    def label_for(self, preset_name: str) -> str:
        for label, preset in self.preset_by_label.items():
            if preset["name"] == preset_name:
                return label
        return next(iter(self.preset_by_label), "")

    @property
    def preset(self) -> dict:
        return self.preset_by_label.get(self.preset_var.get()) or self.presets[0]

    def on_change(self) -> None:
        preset = self.preset
        headless = self.option_vars["DG_HEADLESS"].get() or preset["cacheVariables"].get("DG_HEADLESS") == "ON"
        android = preset["name"].startswith("android")
        apps = ["player"] if android else (["server", "player"] if headless else RUN_APPS)
        self.app_combo.configure(values=apps)
        if self.app_var.get() not in apps:
            self.app_var.set(apps[0])

        notes = []
        if android:
            notes.append("Android builds need ANDROID_NDK_HOME" + ("" if os.environ.get("ANDROID_NDK_HOME")
                                                                  else " (not set!)") + "; they cannot be run here.")
        if "Ninja" in preset["generator"] and not dg.have("ninja"):
            notes.append("Ninja is not installed; pick the Xcode/Visual Studio preset or install Ninja.")
        if headless:
            notes.append("Headless profile: the editor is not built.")
        self.preset_note.configure(text=" ".join(notes))
        self.command_var.set(self.describe(self.build_args()))

    def common_args(self) -> list[str]:
        args = ["--preset", self.preset["name"]]
        cache = dg.read_cache(self.preset["name"])
        for name, var in self.option_vars.items():
            value = "ON" if var.get() else "OFF"
            # Only pass options that differ from what the build tree (or the preset) already uses.
            current = cache.get(name) or self.preset["cacheVariables"].get(name) or \
                ("ON" if OPTION_DEFAULTS[name] else "OFF")
            if value != current.upper():
                args += ["-D", f"{name}={value}"]
        try:
            jobs = int(self.jobs_var.get())
        except (tk.TclError, ValueError):
            jobs = 0
        if jobs > 0:
            args += ["-j", str(jobs)]
        if self.verbose_var.get():
            args.append("--verbose")
        if self.fresh_var.get():
            args.append("--fresh")
        return args

    def config_word(self) -> str:
        return dict(CONFIGS)[self.config_var.get()]

    def build_args(self) -> list[str]:
        args = ["build", self.config_word()]
        if self.target_var.get() != "all":
            args.append(self.target_var.get())
        return args + self.common_args()

    def run_args(self) -> list[str]:
        app_args = []
        if self.run_headless_var.get():
            app_args.append("--headless")
        if self.run_fast_var.get():
            app_args.append("--fast")
        for flag, var in (("--ticks", self.ticks_var), ("--tick-rate", self.tick_rate_var),
                          ("--players", self.players_var), ("--seed", self.seed_var), ("--level", self.level_var)):
            value = var.get().strip()
            if value and not (flag == "--tick-rate" and value == "30") and not (flag == "--players" and value == "2"):
                app_args += [flag, value]
        try:
            app_args += shlex.split(self.extra_args_var.get())
        except ValueError:
            pass
        args = ["run", self.config_word(), self.app_var.get()] + self.common_args()
        return args + (["--", *app_args] if app_args else [])

    def describe(self, args: list[str]) -> str:
        launcher = "dg" if dg.IS_WINDOWS else "./dg"
        return " ".join([launcher, *(shlex.quote(a) for a in args)])

    # -- actions ------------------------------------------------------------------------------------------------------

    def do_setup(self) -> None:
        self.start(["setup"])

    def do_generate(self) -> None:
        self.start(["generate", *self.common_args()])

    def do_ide(self) -> None:
        args = [a for a in self.common_args()]
        name = self.preset["name"]
        if "Ninja" in self.preset["generator"]:  # open the matching IDE preset instead
            ide = {"macos-arm64": "macos-arm64-xcode",
                   "windows-x64": f"windows-x64-vs{dg.vs_product_line() or '2022'}"}.get(name)
            if not ide:
                self.append("[dg] error: no IDE generator for this preset\n", "error")
                return
            args[args.index(name)] = ide
        self.start(["generate", "--ide", *args])

    def do_build(self) -> None:
        self.start(self.build_args())

    def do_run(self) -> None:
        if self.preset["name"].startswith("android"):
            self.append("[dg] error: Android builds cannot be run on this machine\n", "error")
            return
        self.start(self.run_args())

    def do_test(self) -> None:
        args = ["test", self.config_word(), *self.common_args()]
        if self.test_label_var.get() != "all":
            args += ["-L", self.test_label_var.get()]
        self.start(args)

    def do_determinism(self) -> None:
        self.start(["determinism", *self.common_args()])

    def do_clean(self) -> None:
        self.start(["clean", "--preset", self.preset["name"]])

    def open_build_folder(self) -> None:
        folder = dg.preset_dir(self.preset["name"])
        if not folder.exists():
            self.append(f"[dg] {folder} does not exist yet; build first\n", "warn")
            return
        if dg.IS_WINDOWS:
            os.startfile(folder)  # type: ignore[attr-defined]
        elif sys.platform == "darwin":
            subprocess.Popen(["open", str(folder)])
        else:
            subprocess.Popen(["xdg-open", str(folder)])

    # -- process handling ---------------------------------------------------------------------------------------------

    def start(self, args: list[str]) -> None:
        if self.process:
            return
        self.save_settings()
        self.fresh_var.set(False)
        command = [sys.executable, "-u", str(Path(dg.__file__).resolve()), *args]
        self.append(f"\n$ {self.describe(args)}\n", "dg")
        self.status_var.set("Running: " + self.describe(args))
        env = dict(os.environ, PYTHONUNBUFFERED="1")
        kwargs: dict = {}
        if dg.IS_WINDOWS:
            kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP  # type: ignore[attr-defined]
        else:
            kwargs["start_new_session"] = True
        self.process = subprocess.Popen(command, cwd=dg.ROOT, env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, text=True,
                                        encoding="utf-8", errors="replace", bufsize=1, **kwargs)
        self.set_running(True)
        threading.Thread(target=self.read_output, args=(self.process,), daemon=True).start()

    def read_output(self, process: subprocess.Popen) -> None:
        assert process.stdout is not None
        for line in process.stdout:
            self.output.put(line)
        process.wait()
        self.output.put(None)

    def pump_output(self) -> None:
        try:
            while True:
                line = self.output.get_nowait()
                if line is None:
                    self.finish()
                    continue
                if self.hide_noise_var.get() and (any(n in line for n in NOISE) or line.strip() in ("--", "Code: 3")):
                    continue
                lower = line.lower()
                tag = None
                if line.startswith("[dg] error") or "error:" in lower or "failed" in lower or "desync" in lower:
                    tag = "error"
                elif "warning" in lower or line.startswith("[warn]"):
                    tag = "warn"
                elif line.startswith("[dg]") or line.startswith("$ "):
                    tag = "dg"
                elif "passed" in lower or "bit-identical" in lower:
                    tag = "ok"
                self.append(line, tag)
        except queue.Empty:
            pass
        self.after(50, self.pump_output)

    def finish(self) -> None:
        code = self.process.returncode if self.process else 0
        self.process = None
        self.set_running(False)
        self.preset_by_label = {f"{p['displayName']}  [{p['name']}]": p for p in dg.host_presets()}
        if code == 0:
            self.append("✔ finished\n", "ok")
            self.status_var.set("Finished successfully")
        else:
            self.append(f"✘ exited with code {code}\n", "error")
            self.status_var.set(f"Failed (exit code {code})")
        self.on_change()

    def stop(self) -> None:
        if not self.process:
            return
        self.append("[dg] stopping...\n", "warn")
        try:
            if dg.IS_WINDOWS:
                self.process.send_signal(signal.CTRL_BREAK_EVENT)  # type: ignore[attr-defined]
            else:
                os.killpg(self.process.pid, signal.SIGINT)
        except (ProcessLookupError, OSError):
            pass
        self.after(3000, self.force_kill)

    def force_kill(self) -> None:
        if self.process and self.process.poll() is None:
            if dg.IS_WINDOWS:
                self.process.kill()
            else:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except (ProcessLookupError, OSError):
                    pass

    def set_running(self, running: bool) -> None:
        for button in self.action_buttons:
            button.configure(state="disabled" if running else "normal")
        self.stop_button.configure(state="normal" if running else "disabled")

    # -- console ------------------------------------------------------------------------------------------------------

    def append(self, text: str, tag: str | None = None) -> None:
        self.console.configure(state="normal")
        self.console.insert("end", text, (tag,) if tag else ())
        self.console.see("end")
        self.console.configure(state="disabled")

    def clear_console(self) -> None:
        self.console.configure(state="normal")
        self.console.delete("1.0", "end")
        self.console.configure(state="disabled")

    def copy_command(self) -> None:
        self.clipboard_clear()
        self.clipboard_append(self.command_var.get())
        self.status_var.set("Command copied to the clipboard")

    # -- settings -----------------------------------------------------------------------------------------------------

    def load_settings(self) -> dict:
        try:
            return json.loads(SETTINGS_FILE.read_text())
        except (OSError, ValueError):
            return {}

    def save_settings(self) -> None:
        settings = {
            "preset": self.preset["name"], "config": self.config_var.get(), "target": self.target_var.get(),
            "options": {name: var.get() for name, var in self.option_vars.items()},
            "jobs": self.safe_int(self.jobs_var), "verbose": self.verbose_var.get(), "app": self.app_var.get(),
            "run_headless": self.run_headless_var.get(), "run_fast": self.run_fast_var.get(),
            "ticks": self.ticks_var.get(), "tick_rate": self.tick_rate_var.get(), "players": self.players_var.get(),
            "seed": self.seed_var.get(), "level": self.level_var.get(), "extra_args": self.extra_args_var.get(),
            "test_label": self.test_label_var.get(), "hide_noise": self.hide_noise_var.get(),
        }
        try:
            SETTINGS_FILE.parent.mkdir(exist_ok=True)
            SETTINGS_FILE.write_text(json.dumps(settings, indent=2))
        except OSError:
            pass

    @staticmethod
    def safe_int(var: tk.IntVar) -> int:
        try:
            return int(var.get())
        except (tk.TclError, ValueError):
            return 0

    def on_close(self) -> None:
        self.save_settings()
        if self.process:
            self.force_kill_now()
        self.destroy()

    def force_kill_now(self) -> None:
        assert self.process is not None
        try:
            if dg.IS_WINDOWS:
                self.process.kill()
            else:
                os.killpg(self.process.pid, signal.SIGKILL)
        except (ProcessLookupError, OSError):
            pass


def bring_to_front(window: tk.Tk) -> None:
    # Started from a terminal, a Tk window on macOS opens behind the active app; raise it once, then let it behave.
    window.lift()
    window.attributes("-topmost", True)
    window.after(500, lambda: window.attributes("-topmost", False))
    window.focus_force()


def main() -> None:
    print("[dg] Detergine Launcher is open (close its window to return here)", flush=True)
    app = Launcher()
    app.after(100, lambda: bring_to_front(app))
    app.mainloop()
    print("[dg] Detergine Launcher closed", flush=True)


if __name__ == "__main__":
    main()
