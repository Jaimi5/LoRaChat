"""Checks that the sanitizers of [env:native] stop a test run on every class of error they cover.

PlatformIO marks a native suite as errored when its program exits non-zero. Each case below
compiles a small faulty program with the build flags of [env:native] and the sanitizer options
linked into every suite (test/sanitizerOptions.cpp), and expects a non-zero exit with the
sanitizer's report.
"""
import configparser
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
OPTIONS_SOURCE = ROOT / "test" / "sanitizerOptions.cpp"

CASES = {
    "signed_overflow": (
        """
        #include <climits>
        #include <cstdlib>
        int main() {
            volatile int value = INT_MAX - std::rand() % 1;
            value = value + 1;
            return value == 0;
        }
        """,
        "signed integer overflow",
    ),
    "division_by_zero": (
        """
        #include <cstdlib>
        int main() {
            volatile double divisor = std::rand() % 1;
            volatile double ratio = 1.0 / divisor;
            return ratio > 0.0 ? 0 : 0;
        }
        """,
        "division by zero",
    ),
    "leak": (
        """
        #include <cstdlib>
        void* volatile kept;
        int main() {
            kept = std::malloc(64);
            kept = nullptr;
            return 0;
        }
        """,
        "detected memory leaks",
    ),
    "heap_overflow": (
        """
        #include <cstdlib>
        int main() {
            volatile char* buffer = static_cast<char*>(std::malloc(8));
            buffer[8] = 1;
            std::free(const_cast<char*>(buffer));
            return 0;
        }
        """,
        "heap-buffer-overflow",
    ),
    "stack_use_after_return": (
        """
        int* volatile escaped;
        __attribute__((noinline)) void keepLocal() {
            int local = 1;
            escaped = &local;
        }
        int main() {
            keepLocal();
            return *escaped;
        }
        """,
        "stack-use-after-return",
    ),
    "vector_out_of_range": (
        """
        #include <vector>
        int main() {
            std::vector<int> values;
            values.reserve(8);
            values.push_back(1);
            volatile int read = values[1];
            return read & 0;
        }
        """,
        "__n < this->size()",
    ),
}


def native_build_flags():
    """Returns the build_flags of [env:native] without the include paths of the test tree."""
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.ini")
    flags = shlex.split(config["env:native"]["build_flags"])
    return [flag for flag in flags if flag != "-I" and not flag.startswith("test/")]


@pytest.mark.skipif(shutil.which("g++") is None, reason="host g++ not available")
@pytest.mark.parametrize("case", sorted(CASES))
def test_sanitizers_stop_the_run(case, tmp_path):
    source, report = CASES[case]
    program = tmp_path / f"{case}.cpp"
    program.write_text(source)
    binary = tmp_path / case

    build = subprocess.run(
        ["g++", *native_build_flags(), str(program), str(OPTIONS_SOURCE), "-o", str(binary)],
        capture_output=True,
        text=True,
    )
    assert build.returncode == 0, build.stderr

    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, env={})
    assert run.returncode != 0, f"{case} exited 0:\n{run.stderr}"
    assert report in run.stderr
