#!/usr/bin/env python3
"""Static analysis of the firmware sources (src/ and lib/) with clang-tidy and cppcheck.

  static_check.py clang-tidy --build-dir <dir>   needs <dir>/idedata.json (pio run -e tbeam -t idedata)
  static_check.py cppcheck                        needs no build

Both exit 1 on any finding. Suppressions are inline only: `// NOLINT(<check>): <reason>` and
`// cppcheck-suppress <id> ; <reason>`.

Clang has no Xtensa frontend, so clang-tidy parses the ESP32 translation units for a 32-bit
little-endian stand-in target with the include paths and defines of the PlatformIO build. GCC's
own Xtensa header directory is left out because clang cannot parse it; clang uses its builtin
headers instead.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATTERNS = ["src/*.cpp", "lib/*/src/*.cpp"]
PROJECT_INCLUDE_DIRS = ["src", "include", "lib"]

TARGET_ARGS = [
    "--target=i386-unknown-none-elf",
    "-nostdlibinc",
    "-nostdinc++",
    "-funsigned-char",
    "-fexceptions",
    "-fno-rtti",
    "-D__XTENSA__=1",
    "-D__XTENSA_EL__=1",
    "-D__XTENSA_WINDOWED_ABI__=1",
    "-D_GLIBCXX_HAVE_TLS=1",
]
GCC_BUILTIN_INCLUDE = re.compile(r"/lib/gcc/xtensa-esp32-elf/[^/]+/include$")

CPPCHECK_ARGS = [
    "--std=c++20",
    "--language=c++",
    "--enable=warning,performance,portability",
    "--inline-suppr",
    "--error-exitcode=1",
    "--quiet",
    "--suppress=missingIncludeSystem",
    "--template={file}:{line}: {severity}: {message} [{id}]",
    "-D", "T_BEAM",
    "-D", "ESP32",
    "-D", "ARDUINO=10812",
    "-D", "MACSTR=\"%02x:%02x:%02x:%02x:%02x:%02x\"",
    "-D", "MAC2STR(a)=(a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]",
]


def source_files(root=ROOT):
    """Returns the C++ sources of the firmware that git tracks or would track, relative to root."""
    out = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *SOURCE_PATTERNS],
        cwd=root, capture_output=True, text=True, check=True).stdout
    return sorted({line for line in out.splitlines() if line and (root / line).is_file()})


def as_list(flags):
    return flags.split() if isinstance(flags, str) else list(flags)


def is_project_path(path, root):
    try:
        relative = Path(path).resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return bool(relative.parts) and relative.parts[0] in PROJECT_INCLUDE_DIRS


def clang_tidy_args(idedata, root=ROOT):
    """Builds the compiler arguments of one ESP32 translation unit from PlatformIO's idedata."""
    args = list(TARGET_ARGS)
    args += [flag for flag in as_list(idedata.get("cxx_flags", [])) if flag.startswith("-std=")]
    args += [f"-D{define}" for define in idedata.get("defines", [])]
    includes = idedata.get("includes", {})
    paths = list(includes.get("build", [])) + list(includes.get("toolchain", []))
    for path in paths:
        if GCC_BUILTIN_INCLUDE.search(path.rstrip("/")):
            continue
        args += ["-I" if is_project_path(path, root) else "-isystem", path]
    return args


def header_filter(root=ROOT):
    return "^" + re.escape(str(root.resolve())) + "/(src|lib)/"


def run_clang_tidy(binary, source, args, root):
    command = [binary, "--quiet", f"--header-filter={header_filter(root)}", source, "--", *args]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    findings = [line for line in result.stdout.splitlines()
                if re.search(r": (warning|error): ", line)]
    if result.returncode != 0 and not findings:
        findings = [f"{source}: error: clang-tidy exited {result.returncode}",
                    *result.stderr.splitlines()[-5:]]
    return findings


def clang_tidy(options):
    idedata_path = Path(options.build_dir).expanduser() / "idedata.json"
    if not idedata_path.is_file():
        print(f"{idedata_path} not found; run: pio run -e tbeam -t idedata", file=sys.stderr)
        return 2
    args = clang_tidy_args(json.loads(idedata_path.read_text()))
    sources = options.files or source_files()
    with ThreadPoolExecutor(max_workers=options.jobs) as pool:
        results = pool.map(lambda s: run_clang_tidy(options.clang_tidy, s, args, ROOT), sources)
        findings = sorted({line for lines in results for line in lines})
    for line in findings:
        print(line)
    print(f"clang-tidy: {len(sources)} files, {len(findings)} findings")
    return 1 if findings else 0


def cppcheck_args(root=ROOT):
    args = list(CPPCHECK_ARGS)
    args += ["-I", "src", "-I", "include"]
    for lib_src in sorted((root / "lib").glob("*/src")):
        args += ["-I", str(lib_src.relative_to(root))]
    return args


def cppcheck(options):
    sources = options.files or source_files()
    result = subprocess.run([options.cppcheck, *cppcheck_args(), f"-j{options.jobs}", *sources],
                            cwd=ROOT, capture_output=True, text=True)
    findings = [line for line in result.stderr.splitlines() if line.strip()]
    for line in findings:
        print(line)
    print(f"cppcheck: {len(sources)} files, exit {result.returncode}")
    return 1 if result.returncode != 0 else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="tool", required=True)
    tidy = sub.add_parser("clang-tidy", help="clang-tidy over src/ and lib/")
    tidy.add_argument("--build-dir", required=True, help="PlatformIO build dir with idedata.json")
    tidy.add_argument("--clang-tidy", default=shutil.which("clang-tidy-18") or "clang-tidy")
    tidy.set_defaults(run=clang_tidy)
    check = sub.add_parser("cppcheck", help="cppcheck over src/ and lib/")
    check.add_argument("--cppcheck", default="cppcheck")
    check.set_defaults(run=cppcheck)
    for command in (tidy, check):
        command.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
        command.add_argument("files", nargs="*", help="only these sources (default: all)")
    options = parser.parse_args(argv)
    return options.run(options)


if __name__ == "__main__":
    sys.exit(main())
