"""Tests of the argument building in scripts/static_check.py."""
import configparser
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("static_check", ROOT / "scripts" / "static_check.py")
static_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(static_check)

GCC = "/home/u/.platformio/packages/toolchain-xtensa-esp32/lib/gcc/xtensa-esp32-elf/8.4.0"
IDEDATA = {
    "cxx_flags": ["-mlongcalls", "-std=gnu++2a", "-fstrict-volatile-bitfields", "-Os"],
    "defines": ["T_BEAM", "ARDUINO=10812", "ESP32"],
    "includes": {
        "build": [str(ROOT / "src"), str(ROOT / "lib" / "OtaCore" / "src"),
                  "/home/u/.platformio/packages/framework-espidf/components/log/include"],
        "toolchain": [f"{GCC}/include", f"{GCC}/include-fixed",
                      "/home/u/.platformio/packages/toolchain-xtensa-esp32/xtensa-esp32-elf/include"],
    },
}


def test_targets_a_32_bit_stand_in_with_the_xtensa_macros():
    args = static_check.clang_tidy_args(IDEDATA, ROOT)
    assert args[:len(static_check.TARGET_ARGS)] == static_check.TARGET_ARGS
    assert "-D__XTENSA__=1" in args


def test_keeps_only_the_language_standard_from_the_gcc_flags():
    args = static_check.clang_tidy_args(IDEDATA, ROOT)
    assert "-std=gnu++2a" in args
    assert "-mlongcalls" not in args
    assert "-fstrict-volatile-bitfields" not in args


def test_passes_the_build_defines():
    args = static_check.clang_tidy_args(IDEDATA, ROOT)
    assert {"-DT_BEAM", "-DARDUINO=10812", "-DESP32"} <= set(args)


def test_project_includes_are_user_paths_and_the_rest_system_paths():
    args = static_check.clang_tidy_args(IDEDATA, ROOT)
    pairs = list(zip(args, args[1:]))
    assert ("-I", str(ROOT / "src")) in pairs
    assert ("-I", str(ROOT / "lib" / "OtaCore" / "src")) in pairs
    log_include = "/home/u/.platformio/packages/framework-espidf/components/log/include"
    assert ("-isystem", log_include) in pairs


def test_drops_the_gcc_builtin_header_directory_only():
    args = static_check.clang_tidy_args(IDEDATA, ROOT)
    assert f"{GCC}/include" not in args
    assert f"{GCC}/include-fixed" in args


def test_header_filter_matches_project_sources_only():
    pattern = static_check.re.compile(static_check.header_filter(ROOT))
    assert pattern.search(str(ROOT / "src" / "mqtt" / "mqttService.h"))
    assert pattern.search(str(ROOT / "lib" / "OtaCore" / "src" / "otaPolicy.h"))
    assert not pattern.search(str(ROOT / ".pio" / "libdeps" / "tbeam" / "LoRaMesher" / "src" / "a.h"))


def test_cppcheck_includes_every_library():
    args = static_check.cppcheck_args(ROOT)
    for lib_src in (ROOT / "lib").glob("*/src"):
        assert str(lib_src.relative_to(ROOT)) in args


def test_sources_are_tracked_firmware_files():
    sources = static_check.source_files(ROOT)
    assert "src/main.cpp" in sources
    assert all(s.startswith(("src/", "lib/")) and s.endswith(".cpp") for s in sources)
    assert not any("/test/" in s for s in sources)


def test_cppcheck_defines_every_macro_of_the_tbeam_build_flags():
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.ini")
    flags = config["env:tbeam"]["build_flags"].split()
    names = [flags[i + 1].split("=")[0] for i, flag in enumerate(flags) if flag == "-D"]
    names += [flag[2:].split("=")[0] for flag in flags if flag.startswith("-D") and len(flag) > 2]
    assert names
    args = static_check.cppcheck_args(ROOT)
    defined = {args[i + 1].split("=")[0] for i, arg in enumerate(args) if arg == "-D"}
    assert set(names) <= defined
