"""PlatformIO pre-build hook that sets the firmware version (esp_app_desc_t.version).

The version is FW_VERSION from the environment or version.txt (MAJOR.MINOR.PATCH, each 0-255),
followed by git build metadata: +g<short sha>, plus .dirty when tracked files have uncommitted
changes. It reaches ESP-IDF as PROJECT_VER. ESP-IDF only reads PROJECT_VER when CMake runs, so
the CMake cache is removed whenever it holds a different version, which makes PlatformIO
reconfigure.
"""
import os
import re
import subprocess
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO)

CORE_VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def core_version(project_dir: Path) -> str:
    value = os.environ.get("FW_VERSION") or (project_dir / "version.txt").read_text().strip()
    match = CORE_VERSION.match(value)
    if not match or any(int(part) > 255 for part in match.groups()):
        raise ValueError(f"firmware version {value!r} is not MAJOR.MINOR.PATCH with parts 0-255")
    return value


def git_metadata(project_dir: Path) -> str:
    def git(*args: str) -> str:
        return subprocess.run(["git", *args], cwd=project_dir, capture_output=True, text=True,
                              check=True).stdout.strip()

    try:
        sha = git("rev-parse", "--short=7", "HEAD")
        dirty = git("status", "--porcelain", "--untracked-files=no")
    except (OSError, subprocess.CalledProcessError):
        return ""
    return f"+g{sha}" + (".dirty" if dirty else "")


def drop_stale_cmake_cache(build_dir: Path, version: str) -> None:
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return
    match = re.search(r"^PROJECT_VER:[A-Z]+=(.*)$", cache.read_text(errors="replace"), re.M)
    if match is None or match.group(1) != version:
        cache.unlink()


project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
try:
    version = core_version(project_dir) + git_metadata(project_dir)
except (OSError, ValueError) as exc:
    print(f"Error: {exc}")
    env.Exit(1)  # noqa: F821

drop_stale_cmake_cache(Path(env.subst("$BUILD_DIR")), version)  # noqa: F821
board = env.BoardConfig()  # noqa: F821
extra_args = board.get("build.cmake_extra_args", "")
board.update("build.cmake_extra_args", f"{extra_args} -DPROJECT_VER={version}".strip())
print(f"Firmware version: {version}")
