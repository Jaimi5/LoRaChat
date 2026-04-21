# configtool — testbed experiment configuration

Manages per-device configuration for the LoRaChat testbed via an
experiment-oriented YAML template. One YAML file is the single source of
truth for an experiment: defaults that apply to every device, plus
per-device overrides. The tool can snapshot the current testbed state into
a YAML, render any YAML into the per-device `change-config-*.sh` scripts
that `gw-upload.sh` consumes, diff two experiments semantically, and
schema-check a YAML before deploying.

For user-facing usage and the worked end-to-end example, see
`scripts/testbed/README.md#experiment-configs`. This file documents the
internals.

## Files

| File           | Role                                                              |
|----------------|-------------------------------------------------------------------|
| `configtool.py`| argparse CLI; dispatches to the subcommand modules                |
| `schema.py`    | `PARAMS` table — the single source of truth for YAML↔`#define` mapping and value formatting |
| `common.py`    | repo paths, `testbed.conf` loader, YAML loader, `flatten()`       |
| `generate.py`  | YAML → one `change-config-{SHORT_ID}.sh` per device               |
| `compare.py`   | semantic diff between two experiment YAMLs                        |
| `validate.py`  | schema check (unknown keys, bad types, device cross-check)        |
| `harvest.py`   | SSH/SCP into every gateway, parse existing scripts + `src/config.h`, emit a baseline YAML |

## The schema (`schema.py`)

Each parameter is a `Param(yaml_key, define, kind, help)`. `kind` controls
how YAML values are rendered back into C literals:

| kind     | Example YAML     | Emitted C literal   |
|----------|------------------|---------------------|
| `int`    | `20`             | `20`                |
| `uint`   | `9`              | `9U`                |
| `float`  | `125.0`          | `125.0`             |
| `floatF` | `869.900` or `"869.900"` | `869.9F` / `869.900F` |
| `hex16`  | `0x77A4`         | `0x77A4`            |
| `str`    | `admin`          | `"admin"`           |

Note: YAML numeric literals lose trailing zeros (PyYAML → Python float →
`repr()`). Quote the value (`"869.900"`) if you need to preserve them —
the C compiler emits identical bits either way.

**Adding a new parameter:** extend `PARAMS` in `schema.py`. Every other
module picks it up automatically because they iterate `PARAMS`. Update the
parameter table in `scripts/testbed/README.md` too.

## Generated script format

```bash
#!/usr/bin/env bash
# AUTO-GENERATED from scripts/testbed/experiments/<name>.yaml — do not hand-edit (re-run configtool generate).
# Experiment: <name>
# Device:     77A4
# Generated:  2026-04-21
set -euo pipefail

CONFIG_H="${CONFIG_H:-src/config.h}"
[[ -f "$CONFIG_H" ]] || { echo "ERROR: $CONFIG_H not found (run from repo root)" >&2; exit 1; }

sed -i -E $'s|^#define LORA_POWER [^\r]*|#define LORA_POWER 17|' "$CONFIG_H"
...
```

Notes on the `sed` line:
- `$'...'` (bash ANSI-C quoting) embeds a literal CR in the pattern.
- `[^\r]*` stops before a trailing CR, so the replacement preserves CRLF
  line endings on files that use them (our `src/config.h` is CRLF-terminated).
  On LF-only files the class simply matches to end-of-line.
- The `&` and `|` chars in replacement values are escaped in `_render_sed_line`.
- `CONFIG_H` is overridable via environment variable — useful for dry-runs:
  `CONFIG_H=/tmp/foo.h bash change-config-77A4.sh`.

## Harvest internals

`harvest.py` groups devices by gateway, then per-gateway:

1. SCPs `src/config.h` back once.
2. For each expected `SHORT_ID`, tries `{REPO}/change-config-{SHORT_ID}.sh`
   first, then `{REPO}/scripts/testbed/change-config-{SHORT_ID}.sh`.
3. Parses `src/config.h` for baseline values of every schema parameter
   (matches top-level `#define NAME VALUE` lines only — not inside `#ifdef`
   blocks, which is fine since every schema parameter is top-level).
4. Parses each `change-config-*.sh` for `sed` overrides. The regex accepts
   both legacy `.*` and the generated `[^\r]*` forms and either `'...'` or
   `$'...'` quoting.
5. Merges: per-device script overrides take precedence over the gateway's
   `src/config.h` baseline.
6. Splits per-device params into `defaults` (values that every device agrees
   on) and per-device override blocks.

Authentication reuses `testbed.conf`'s `GW_KEY` / `GW_PORT` / `GW_PASS`.
Password auth requires `sshpass` (same constraint as `deploy.sh`).

## Locations

| Thing                         | Path                                              | In git? |
|-------------------------------|---------------------------------------------------|---------|
| Experiment YAMLs (real)       | `scripts/testbed/experiments/*.yaml`              | no (gitignored — contain credentials) |
| Sanitised template            | `scripts/testbed/experiments/example-baseline.yaml` | yes (allow-listed) |
| Generated per-device scripts  | `scripts/testbed/change-config/*.sh`              | no (reproducible from YAML) |
| The `#define` target          | `src/config.h`                                    | yes     |
| Testbed device/gateway map    | `scripts/testbed/testbed.conf`                    | yes (tracked with placeholders; `skip-worktree` locally) |
| Upload-time script consumer   | `scripts/testbed/gw-upload.sh` (line ~60)         | yes     |

Deployment: `scripts/testbed/deploy.sh push-config` (or the auto-run wrapped
into `deploy.sh upload` / `deploy.sh all`) validates the YAML, calls
`configtool generate`, then scp's each per-device script to its gateway's
`scripts/testbed/change-config/` dir. The YAML itself never leaves the dev
machine.
