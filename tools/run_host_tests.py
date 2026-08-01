#!/usr/bin/env python3
"""Build and run the host unit tests.

    python tools/run_host_tests.py              # all suites
    python tools/run_host_tests.py protocol     # just test/test_protocol

Why this exists
---------------
`pio test -e native` is the idiomatic command, but PlatformIO's native platform
hardcodes `env.Tool("gcc")` and the SCons it bundles has the MSVC support
module stripped out. On a Windows box with Visual Studio but no MinGW that
combination cannot build anything.

Rather than require a toolchain install, this script drives the MSVC that
Visual Studio already provides: it locates vcvars64.bat through vswhere,
captures the environment it sets up, and compiles each suite directly.

On Linux/macOS it uses g++ and `pio test -e native` works too; this is only
needed on Windows.

Dependencies (Unity, ArduinoJson) come from PlatformIO's own libdeps, so run
`pio pkg install -e native` once first.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
TEST = ROOT / "test"
BUILD = ROOT / ".pio" / "host_tests"

# Translation units with no Arduino dependencies. Linking all of them into
# every suite is simpler than tracking per-suite dependencies and costs
# nothing at this size.
NATIVE_SOURCES = ["text_render.cpp", "protocol.cpp", "animations.cpp", "state.cpp"]


def find_libdep(name: str) -> Path | None:
    """Locate a PlatformIO-installed library, whichever env pulled it in."""
    for env in ("native", "esp32c3"):
        candidate = ROOT / ".pio" / "libdeps" / env / name
        if candidate.is_dir():
            return candidate
    return None


def msvc_environment() -> dict[str, str] | None:
    """Run vcvars64.bat and capture the environment it produces."""
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None

    found = subprocess.run(
        [
            str(vswhere), "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-format", "value", "-property", "installationPath",
        ],
        capture_output=True, text=True, check=False,
    )

    for install in found.stdout.strip().splitlines():
        bat = Path(install.strip()) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        if not bat.exists():
            continue
        dumped = subprocess.run(
            f'call "{bat}" >nul 2>&1 && set',
            shell=True, capture_output=True, text=True, check=False,
        )
        env = {}
        for line in dumped.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                env[key] = value
        if "INCLUDE" in env:
            return env
    return None


def suites(filter_name: str | None) -> list[Path]:
    found = sorted(d for d in TEST.glob("test_*") if d.is_dir())
    if filter_name:
        wanted = filter_name if filter_name.startswith("test_") else f"test_{filter_name}"
        found = [d for d in found if d.name == wanted]
    return found


def build_and_run(suite: Path, env: dict[str, str] | None, includes: list[Path], unity: Path) -> bool:
    out_dir = BUILD / suite.name
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f"{suite.name}.exe"

    sources = sorted(suite.glob("*.cpp")) + [SRC / s for s in NATIVE_SOURCES] + [unity / "unity.c"]

    if env is not None:  # MSVC
        # vcvars dumps a complete environment, so use it as-is. Merging it into
        # os.environ would leave both "Path" and "PATH" set, and Windows treats
        # those as the same variable while Python does not.
        #
        # subprocess also resolves the executable against the *parent's* PATH,
        # not the child environment's, so cl.exe needs an absolute path.
        compiler = shutil.which("cl", path=env.get("PATH", ""))
        if compiler is None:
            print(f"--- {suite.name}: cl.exe not on the vcvars PATH ---", file=sys.stderr)
            return False

        cmd = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W3", "/wd4244", "/wd4267",
               f"/Fo:{out_dir}\\", f"/Fe:{exe}"]
        cmd += [f"/I{path}" for path in includes]
        cmd += [str(s) for s in sources]
        run_env = env
    else:  # g++
        cmd = ["g++", "-std=gnu++17", "-Wall", "-o", str(exe.with_suffix(""))]
        cmd += [f"-I{path}" for path in includes]
        cmd += [str(s) for s in sources]
        run_env = dict(os.environ)
        exe = exe.with_suffix("")

    compiled = subprocess.run(cmd, cwd=out_dir, env=run_env, capture_output=True, text=True, check=False)
    if compiled.returncode != 0:
        print(f"--- {suite.name}: BUILD FAILED ---")
        print(compiled.stdout)
        print(compiled.stderr, file=sys.stderr)
        return False

    result = subprocess.run([str(exe)], cwd=ROOT, check=False)
    return result.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", nargs="?", help="run only this suite, e.g. 'protocol'")
    args = parser.parse_args()

    unity = find_libdep("Unity")
    arduinojson = find_libdep("ArduinoJson")
    if unity is None or arduinojson is None:
        print("missing dependencies -- run:  pio pkg install -e native", file=sys.stderr)
        return 1

    includes = [SRC, unity / "src", arduinojson / "src"]

    env = None
    if sys.platform == "win32":
        env = msvc_environment()
        if env is None:
            if shutil.which("g++") is None:
                print(
                    "no host C++ compiler found.\n"
                    "Install either Visual Studio's C++ workload or MinGW-w64.",
                    file=sys.stderr,
                )
                return 1
    elif shutil.which("g++") is None:
        print("g++ not found", file=sys.stderr)
        return 1

    found = suites(args.suite)
    if not found:
        print(f"no suites matched {args.suite!r}", file=sys.stderr)
        return 1

    failures = []
    for suite in found:
        print(f"\n=== {suite.name} " + "=" * (60 - len(suite.name)), flush=True)
        if not build_and_run(suite, env, includes, unity / "src"):
            failures.append(suite.name)

    print("\n" + "=" * 68)
    if failures:
        print(f"FAILED: {', '.join(failures)}")
        return 1
    print(f"all {len(found)} suites passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
