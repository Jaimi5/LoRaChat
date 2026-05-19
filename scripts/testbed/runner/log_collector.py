"""Move collected logs from deploy.sh's default location into a run directory.

`deploy.sh logs -n SESSION` rsyncs logs into `LOCAL_LOG_DIR/SESSION/` (default
./logs_testbed/SESSION/) on the orchestrator. After each run we move those
files into the run's own directory so they can't be confused with the next
run's output.
"""

from __future__ import annotations

import shutil
from pathlib import Path


def move_session_logs(local_log_dir: Path, session: str, dest_logs_dir: Path) -> int:
    """Move every file in `local_log_dir/session/` into `dest_logs_dir/`.

    Returns the number of files moved. If the source directory does not
    exist or is empty, returns 0.
    """
    src = local_log_dir / session
    if not src.is_dir():
        return 0
    dest_logs_dir.mkdir(parents=True, exist_ok=True)

    moved = 0
    for entry in src.iterdir():
        if entry.is_file():
            shutil.move(str(entry), str(dest_logs_dir / entry.name))
            moved += 1
    # Remove the now-empty session dir; ignore if not empty (rare race).
    try:
        src.rmdir()
    except OSError:
        pass
    return moved
