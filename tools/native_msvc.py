"""Let `pio test -e native` use MSVC on Windows.

PlatformIO's native platform hardcodes `env.Tool("gcc")`, so on a Windows box
with only Visual Studio installed the host tests fail to build. This script
swaps in the MSVC toolchain that Visual Studio already provides, so the tests
run without anyone having to install MinGW.

On Linux/macOS it does nothing and the normal gcc path is used.

Wired up from platformio.ini:  extra_scripts = pre:tools/native_msvc.py
"""

import sys

Import("env")  # noqa: F821  (injected by SCons)

if sys.platform == "win32":
    # SCons ships full MSVC support; these three set up cl.exe, link.exe and
    # lib.exe along with the INCLUDE/LIB environment they need.
    for tool in ("msvc", "mslink", "mslib"):
        env.Tool(tool)

    env.Append(
        CXXFLAGS=["/std:c++17", "/EHsc"],
        CCFLAGS=["/nologo", "/W3"],
        LINKFLAGS=["/nologo"],
    )
else:
    env.Append(CXXFLAGS=["-std=gnu++17"], CCFLAGS=["-Wall"])
