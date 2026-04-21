"""Render an experiment YAML into per-device change-config-*.sh scripts."""

from __future__ import annotations

import os
import stat
from datetime import date
from pathlib import Path
from typing import Any

from common import CHANGE_CONFIG_DIR, flatten, load_yaml
from schema import param


SCRIPT_HEADER = """\
#!/usr/bin/env bash
# AUTO-GENERATED from {source} — do not hand-edit (re-run configtool generate).
# Experiment: {experiment}
# Device:     {short_id}
# Generated:  {today}
set -euo pipefail

CONFIG_H="${{CONFIG_H:-src/config.h}}"
[[ -f "$CONFIG_H" ]] || {{ echo "ERROR: $CONFIG_H not found (run from repo root)" >&2; exit 1; }}
"""


def _render_sed_line(define: str, c_literal: str) -> str:
    # `[^\r]*` (with a literal CR inside `$'...'` quoting) stops before the
    # trailing CR on CRLF-terminated lines, so the replacement doesn't strip
    # `\r` and leave a mixed-line-ending file. The pattern still works on
    # LF-only files because there's no CR to avoid.
    esc = c_literal.replace("\\", "\\\\").replace("&", "\\&").replace("|", "\\|")
    return f'sed -i -E $\'s|^#define {define} [^\\r]*|#define {define} {esc}|\' "$CONFIG_H"'


def generate(yaml_path: Path, out_dir: Path | None = None) -> list[Path]:
    """Render the experiment at `yaml_path` into one change-config-*.sh file
    per device. Returns the list of written paths."""
    if out_dir is None:
        out_dir = CHANGE_CONFIG_DIR
    out_dir.mkdir(parents=True, exist_ok=True)

    experiment = load_yaml(yaml_path)
    exp_name = experiment.get("experiment") or yaml_path.stem
    flat = flatten(experiment)
    today = date.today().isoformat()

    written: list[Path] = []
    for short_id, params in flat.items():
        lines = [
            SCRIPT_HEADER.format(
                source=yaml_path.relative_to(yaml_path.anchor) if yaml_path.is_absolute() else yaml_path,
                experiment=exp_name,
                short_id=short_id,
                today=today,
            )
        ]
        # Sort by YAML key so diffs are stable across runs.
        for key in sorted(params):
            p = param(key)  # raises KeyError on unknown keys
            literal = p.format_c(params[key])
            lines.append(_render_sed_line(p.define, literal))

        out_path = out_dir / f"change-config-{short_id}.sh"
        content = "\n".join(lines) + "\n"
        out_path.write_text(content)
        os.chmod(out_path, out_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        written.append(out_path)

    return written
