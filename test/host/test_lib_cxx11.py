"""Builds the pure-logic libraries in lib/ with -std=gnu++11 and links them.

ESP-IDF 4.4 compiles C++ as gnu++11, while the native GoogleTest env uses gnu++17. In C++11 a
static constexpr data member that is bound to a reference (std::min, push_back) needs an
out-of-class definition, otherwise the link fails. This test links every source of each
library at -O0 so no such use is optimised away.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
LIBS = ["OtaCore", "NetCore", "CmdCore"]


@pytest.mark.skipif(shutil.which("g++") is None, reason="host g++ not available")
@pytest.mark.parametrize("lib", LIBS)
def test_library_links_as_gnu_cxx11(lib, tmp_path):
    src_dir = ROOT / "lib" / lib / "src"
    sources = sorted(src_dir.glob("*.cpp"))
    assert sources, f"no sources in {src_dir}"

    main = tmp_path / "main.cpp"
    main.write_text("int main() { return 0; }\n")

    result = subprocess.run(
        ["g++", "-std=gnu++11", "-O0", "-Wall", "-I", str(src_dir),
         *map(str, sources), str(main), "-o", str(tmp_path / "a.out")],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
