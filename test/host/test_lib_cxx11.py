"""Builds the pure-logic libraries in lib/ as gnu++11 and gnu++17 with strict warnings and links them.

ESP-IDF 4.4 compiles C++ as gnu++11, while the native GoogleTest env uses gnu++17. In C++11 a
static constexpr data member that is bound to a reference (std::min, push_back) needs an
out-of-class definition, otherwise the link fails. This test links every source of each
library at -O0 so no such use is optimised away. Any warning fails the build.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
LIBS = ["OtaCore", "NetCore", "CmdCore"]
STANDARDS = ["gnu++11", "gnu++17"]
WARNINGS = [
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Werror",
]


@pytest.mark.skipif(shutil.which("g++") is None, reason="host g++ not available")
@pytest.mark.parametrize("standard", STANDARDS)
@pytest.mark.parametrize("lib", LIBS)
def test_library_links_without_warnings(lib, standard, tmp_path):
    src_dir = ROOT / "lib" / lib / "src"
    sources = sorted(src_dir.glob("*.cpp"))
    assert sources, f"no sources in {src_dir}"

    main = tmp_path / "main.cpp"
    main.write_text("int main() { return 0; }\n")

    result = subprocess.run(
        ["g++", f"-std={standard}", "-O0", *WARNINGS, "-I", str(src_dir),
         *map(str, sources), str(main), "-o", str(tmp_path / "a.out")],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
