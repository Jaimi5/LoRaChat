#!/usr/bin/env python3
"""Collect task stack warnings from testbed monitor logs.

For every run dir given, reads logs/monitor-dev-*-<NODE>.log[.gz] and writes to
<out_dir>/<run name>/:
  warnings.log  every "[WARNING] Task <name> stack low: <N> bytes free" line,
                prefixed with the node id (raw timestamped log lines)
  stack.csv     per (node, task): configured bytes and min/last free bytes
                from the "STACK[<name>] total=<bytes> free=<bytes>" lines
  summary.md    per task across all nodes: worst-case free, number of warnings

LoRaMesher 2.0.0 reports both values in bytes; 134ae25 reported free in words
(4x inflated), so do not mix runs of the two versions in one table.

Usage: grab_stack_warnings.py <run_dir> [<run_dir> ...] --out <out_dir>
"""
import argparse
import csv
import gzip
import re
from collections import defaultdict
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
NODE = re.compile(r"monitor-dev-.*-([0-9A-Fa-f]{4})\.log(?:\.gz)?$")
WARN = re.compile(r"\[WARNING\].*Task (\S+) stack low: (\d+) bytes free")
STACK = re.compile(r"STACK\[([^\]]+)\] total=(\d+) free=(\d+)")


def open_log(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", errors="replace")
    return open(path, errors="replace")


def grab(run_dir: Path, out_root: Path) -> dict:
    out = out_root / run_dir.name
    out.mkdir(parents=True, exist_ok=True)
    warn_lines = []
    stack = {}  # (node, task) -> [total, min_free, last_free, n]
    warn_count = defaultdict(int)  # task -> n warnings
    warn_min = {}  # task -> min free reported in a warning
    for log in sorted((run_dir / "logs").glob("monitor-dev-*")):
        m = NODE.search(log.name)
        if not m:
            continue
        node = m.group(1).upper()
        with open_log(log) as fh:
            for raw in fh:
                line = ANSI.sub("", raw).rstrip("\n")
                w = WARN.search(line)
                if w:
                    task, free = w.group(1), int(w.group(2))
                    warn_lines.append(f"{node}  {line}")
                    warn_count[task] += 1
                    warn_min[task] = min(free, warn_min.get(task, free))
                    continue
                s = STACK.search(line)
                if s:
                    task, total, free = s.group(1), int(s.group(2)), int(s.group(3))
                    e = stack.setdefault((node, task), [total, free, free, 0])
                    e[1] = min(e[1], free)
                    e[2] = free
                    e[3] += 1

    (out / "warnings.log").write_text("\n".join(warn_lines) + ("\n" if warn_lines else ""))
    with open(out / "stack.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["node", "task", "configured_bytes", "min_free_bytes", "last_free_bytes",
                     "samples"])
        for (node, task), (total, mn, last, n) in sorted(stack.items()):
            wr.writerow([node, task, total, mn, last, n])

    per_task = defaultdict(lambda: {"total": set(), "min": None, "node": None, "nodes": set()})
    for (node, task), (total, mn, _last, _n) in stack.items():
        t = per_task[task]
        t["total"].add(total)
        t["nodes"].add(node)
        if t["min"] is None or mn < t["min"]:
            t["min"], t["node"] = mn, node
    md = [f"# Stack usage — {run_dir.name}", "",
          f"Source: `{run_dir}`. {len(warn_lines)} `[WARNING] Task … stack low` line(s).", "",
          "| Task | Configured (B) | Worst min free (B) | Worst node | Nodes | Warnings |",
          "|---|---|---|---|---|---|"]
    for task in sorted(set(per_task) | set(warn_count)):
        t = per_task.get(task)
        total = "/".join(str(x) for x in sorted(t["total"])) if t else "?"
        worst = t["min"] if t else warn_min.get(task, "?")
        md.append(f"| {task} | {total} | {worst} | {t['node'] if t else '?'} | "
                  f"{len(t['nodes']) if t else '?'} | {warn_count.get(task, 0)} |")
    (out / "summary.md").write_text("\n".join(md) + "\n")
    return {"run": run_dir.name, "warnings": len(warn_lines), "tasks": len(per_task)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("runs", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    for run in a.runs:
        if not (run / "logs").is_dir():
            print(f"skip {run}: no logs/ dir")
            continue
        r = grab(run, a.out)
        print(f"{r['run']}: {r['warnings']} warning line(s), {r['tasks']} task(s) -> {a.out / r['run']}")


if __name__ == "__main__":
    main()
