"""Non-destructive run exclusion list: scripts/testbed/runs/EXCLUDED.txt.

One run directory NAME per line; `#` starts a comment (whole line or after the
name, where it is the reason). Excluded runs keep their directories; multirun.py,
lm200/gates.py and lm200/report.py skip them and say so. Example:

    # Runs excluded from aggregates and gates. Directories are kept.
    # <run dir name>  # reason
    lm200ab_13node-20261008-210609-SF12_d308000-r02  # host suspended: 538-min window

The list is read from <runs root>/EXCLUDED.txt, where the runs root is the
parent of a run's batch dir (normally scripts/testbed/runs/).
"""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path

FILENAME = "EXCLUDED.txt"


@lru_cache(maxsize=None)
def _load(path: str, mtime: float) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        name, _, reason = line.partition("#")
        name = name.strip()
        if name:
            out[name] = reason.strip() or "listed in EXCLUDED.txt"
    return out


def load_excluded(runs_root: Path) -> dict[str, str]:
    """{run dir name: reason} from runs_root/EXCLUDED.txt ({} when absent)."""
    p = Path(runs_root) / FILENAME
    try:
        return _load(str(p), p.stat().st_mtime)
    except OSError:
        return {}


def excluded_reason(run_dir: Path) -> str | None:
    """Why `run_dir` (runs/<batch>/<run>) is excluded, or None."""
    run_dir = Path(run_dir)
    return load_excluded(run_dir.parent.parent).get(run_dir.name)
