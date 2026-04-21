#!/usr/bin/env python3
"""Testbed config template tool.

Subcommands:
  harvest   — pull change-config-*.sh + src/config.h from every gateway and
              emit scripts/testbed/experiments/baseline-YYYY-MM-DD.yaml
  generate  — render an experiment YAML into per-device change-config-*.sh
              scripts under scripts/testbed/change-config/
  diff      — semantic diff between two experiment YAMLs
  validate  — schema-check an experiment YAML
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow running as `python scripts/testbed/configtool/configtool.py ...`
# without installing the package.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from compare import diff as do_diff
from generate import generate as do_generate
from harvest import harvest as do_harvest
from validate import validate as do_validate


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="configtool",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_harvest = sub.add_parser("harvest", help="snapshot current testbed state into a YAML")
    p_harvest.add_argument("-o", "--output", type=Path, default=None,
                           help="output YAML path (default: experiments/baseline-YYYY-MM-DD.yaml)")
    p_harvest.add_argument("-n", "--name", default=None,
                           help="experiment name to embed in the YAML (default: baseline-YYYY-MM-DD)")

    p_gen = sub.add_parser("generate", help="render YAML into per-device change-config-*.sh scripts")
    p_gen.add_argument("yaml", type=Path, help="path to the experiment YAML")
    p_gen.add_argument("-o", "--out-dir", type=Path, default=None,
                       help="output dir (default: scripts/testbed/change-config)")

    p_diff = sub.add_parser("diff", help="semantic diff between two experiment YAMLs")
    p_diff.add_argument("a", type=Path)
    p_diff.add_argument("b", type=Path)

    p_val = sub.add_parser("validate", help="schema-check an experiment YAML")
    p_val.add_argument("yaml", type=Path)
    p_val.add_argument("--strict-devices", action="store_true",
                       help="also require every YAML device to be listed in testbed.conf DEVICES")

    args = parser.parse_args(argv)

    if args.cmd == "harvest":
        do_harvest(args.output, args.name)
        return 0

    if args.cmd == "generate":
        written = do_generate(args.yaml, args.out_dir)
        print(f"wrote {len(written)} script(s):")
        for p in written:
            print(f"  {p}")
        return 0

    if args.cmd == "diff":
        n, text = do_diff(args.a, args.b)
        print(text)
        return 1 if n else 0

    if args.cmd == "validate":
        errors = do_validate(args.yaml, strict_devices=args.strict_devices)
        if not errors:
            print(f"{args.yaml}: OK")
            return 0
        for e in errors:
            print(f"{args.yaml}: {e}")
        return 1

    return 2


if __name__ == "__main__":
    sys.exit(main())
