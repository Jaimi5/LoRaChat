"""Semantic diff between two experiment YAMLs."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from common import HexInt, flatten, load_yaml
from schema import PARAMS


def _fmt(key: str, value: Any) -> str:
    # Render hex16-typed values as 0xNNNN in diff output regardless of how
    # they came off the YAML parser (HexInt vs plain int).
    p = PARAMS.get(key)
    if p is not None and p.kind == "hex16" and isinstance(value, int) and not isinstance(value, bool):
        return f"0x{value:04X}"
    return repr(value)


def diff(a_path: Path, b_path: Path) -> tuple[int, str]:
    """Return (n_changes, rendered_diff_text). Exit 0 if identical, 1 otherwise."""
    a = flatten(load_yaml(a_path))
    b = flatten(load_yaml(b_path))

    lines: list[str] = []
    lines.append(f"--- {a_path}")
    lines.append(f"+++ {b_path}")

    all_devices = sorted(set(a) | set(b))
    total = 0
    for short_id in all_devices:
        if short_id not in a:
            lines.append(f"\n[{short_id}] (added)")
            for k in sorted(b[short_id]):
                lines.append(f"    + {k} = {_fmt(k, b[short_id][k])}")
                total += 1
            continue
        if short_id not in b:
            lines.append(f"\n[{short_id}] (removed)")
            for k in sorted(a[short_id]):
                lines.append(f"    - {k} = {_fmt(k, a[short_id][k])}")
                total += 1
            continue
        pa, pb = a[short_id], b[short_id]
        device_changes: list[str] = []
        for k in sorted(set(pa) | set(pb)):
            va, vb = pa.get(k, _MISSING), pb.get(k, _MISSING)
            if va == vb:
                continue
            total += 1
            if va is _MISSING:
                device_changes.append(f"    + {k} = {_fmt(k, vb)}")
            elif vb is _MISSING:
                device_changes.append(f"    - {k} = {_fmt(k, va)}")
            else:
                device_changes.append(f"    ~ {k}: {_fmt(k, va)} → {_fmt(k, vb)}")
        if device_changes:
            lines.append(f"\n[{short_id}]")
            lines.extend(device_changes)

    if total == 0:
        lines.append("\n(no differences)")
    else:
        lines.append(f"\n{total} change(s)")

    return total, "\n".join(lines)


_MISSING = object()
