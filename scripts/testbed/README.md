# Testbed Deployment Scripts

Automate firmware deployment and monitoring across the LoRaChat testbed: 13 ESP32 devices on 6 gateway machines.

## Architecture

```
Your Machine                    Gateway Machines (SSH)
┌──────────┐                   ┌───────────────────────────────────┐
│          │   SSH (parallel)  │ GW-1                              │
│ deploy.sh│──────────────────>│  ├─ /home/lora/dev/lora-E464 → E464│
│          │                   │  ├─ /home/lora/dev/lora-77A4 → 77A4│
│          │                   │  └─ /home/lora/dev/lora-DF10 → DF10│
│          │                   ├───────────────────────────────────┤
│          │──────────────────>│ GW-2                              │
│          │                   │  ├─ /home/lora/dev/lora-006C → 006C│
│          │                   │  └─ /home/lora/dev/lora-7B6C → 7B6C│
│          │                   ├───────────────────────────────────┤
│          │──────────────────>│ GW-3 ... GW-6                    │
└──────────┘                   └───────────────────────────────────┘
```

Operations run **in parallel across gateways** but **sequentially per device within each gateway** (PlatformIO limitation: one build at a time).

The console shows a clean spinner display with per-gateway progress. All detailed SSH output goes to log files under `logs_testbed/{SESSION}/` for later inspection.

```
Upgrading all gateways (branch: new_loramesher)...

  GW-1  | Running...    (12s)
  GW-2  ✓ OK            (15s)
  GW-3  / Running...    (10s)
  GW-4  ✗ FAIL          (8s)  → logs_testbed/cap2-v2/GW-4-upgrade.log
  GW-5  - Running...    (6s)
  GW-6  ✓ OK            (14s)
```

## Prerequisites

### On all gateway machines:

1. **PlatformIO Core** installed:
   ```bash
   pip install platformio
   ```

2. **Serial port permissions** (add user to dialout group):
   ```bash
   sudo usermod -aG dialout $USER
   # Log out and back in
   ```

3. **Git credentials** configured (SSH key or cached HTTPS token) for `git pull`

4. **LoRaChat repo** cloned at `~/LoRaChat`:
   ```bash
   git clone <repo-url> ~/LoRaChat
   ```

5. **Per-device configs**. Per-device values (LoRa power/SF/BW, `LORA_MANAGER_ID`, WiFi/MQTT credentials, etc.) are defined in an experiment YAML under `scripts/testbed/experiments/` and rendered into `change-config-{SHORT_ID}.sh` scripts by `configtool generate`. See [Experiment configs](#experiment-configs) for the workflow. Legacy hand-written `change-config-*.sh` in the repo root still work — `gw-upload.sh` checks the repo root, then `scripts/testbed/change-config/` (generator output), then `scripts/testbed/`.

6. **Serial port symlinks** set up for each connected device (see [Serial Port Setup](#serial-port-setup) below)

### On your machine:

1. **SSH authentication** — choose one:
   - **Password auth** (simpler): install `sshpass` and fill in `GW_PASS` in `testbed.conf`
     ```bash
     apt install sshpass
     ```
   - **SSH keys** (more secure): copy keys to all gateways
     ```bash
     ssh-copy-id lora@gw1-ip
     ssh-copy-id lora@gw2-ip
     # ... for all gateways
     ```

2. **Bash 4+** (for associative arrays):
   ```bash
   bash --version  # Must be 4.0+
   ```

## Configuration

Edit `testbed.conf` to set up your testbed:

### 1. Set gateway SSH destinations

```bash
declare -A GW_SSH=(
    [GW-1]="lora@192.168.1.101"
    [GW-2]="lora@192.168.1.102"
    ...
)
```

### 2. Map devices to gateways

Each device just needs its DEVICE_ID and GW assignment. The serial port is
auto-generated from the device ID using the naming convention:

```
/home/lora/dev/lora-{SHORT_ID}
```

where `SHORT_ID` is the last 4 hex characters of the device name (e.g., `C6E104-77A4` → `lora-77A4`).

```bash
DEVICES=(
    "C6E104-E464:GW-1"
    "C6E104-77A4:GW-1"
    ...
)
```

The `SERIAL_PORT_PREFIX` in `testbed.conf` controls the path prefix (default: `/home/lora/dev/lora-`).

To verify the symlinks exist on a gateway:
```bash
ssh lora@gw-ip "ls -la /home/lora/dev/lora-*"
```

### 3. Verify connectivity

```bash
./deploy.sh status
```

## Experiment configs

Per-device configuration (LoRa RF parameters, mesh identity, WiFi/MQTT
credentials) is described in a YAML file per experiment under
`scripts/testbed/experiments/`. `deploy.sh` reads that YAML, generates a
`change-config-{SHORT_ID}.sh` per device locally, and pushes the relevant
scripts to each gateway via `scp`. The YAML — which contains credentials —
never leaves your machine and is gitignored.

Two experiments are comparable by diffing their YAMLs (with plain
`git diff` or `configtool diff` for a semantic device-by-device view).

### Secrets hygiene — read this first

Real experiment YAMLs contain WiFi/MQTT credentials; `testbed.conf`
contains gateway SSH passwords. **None of these should reach git.** The
repo is set up so:

- `scripts/testbed/experiments/*.yaml` is gitignored, except the sanitised
  `example-baseline.yaml` template.
- `scripts/testbed/change-config/*.sh` (generator output) is gitignored —
  reproducible from the YAML.
- `scripts/testbed/testbed.conf` is tracked with placeholder values. To
  protect your local edits from accidental commits, run **once**:
  ```bash
  git update-index --skip-worktree scripts/testbed/testbed.conf
  ```
  Git will then ignore your local changes to the file. To pick up upstream
  changes later: `git update-index --no-skip-worktree scripts/testbed/testbed.conf`
  then rebase/merge as normal.

### YAML layout

```yaml
experiment: exp-001-sf7-high-power
description: "SF7, 20 dBm, baseline mesh"
defaults:                          # applied to every device
  lora_frequency: 869.900
  lora_spreading_factor: 9
  lora_bandwidth: 125.0
  lora_coding_rate: 7
  lora_power: 17
  lora_sync_word: 20
  wifi_ssid: YOUR_SSID
  wifi_password: YOUR_PASSWORD
  mqtt_server: YOUR_BROKER
  mqtt_port: 1883
  mqtt_username: YOUR_USER
  mqtt_password: YOUR_PASS
  mqtt_topic_sub: from-server/
  mqtt_topic_out: to-server/
devices:                           # per-device overrides; keys are SHORT_IDs
  E464: { lora_manager_id: 0xE464 }
  77A4: { lora_manager_id: 0x77A4, lora_power: 20 }   # overrides default power
  # ... one entry per SHORT_ID listed in testbed.conf DEVICES
```

A device's effective config is `defaults` merged with its per-device block.
Per-device keys win.

> **Note on numeric formatting.** YAML parses `869.900` as a Python float
> which is rendered back as `869.9` in the generated C. This is the same
> IEEE-754 value and the LoRa stack is indifferent. If you want the literal
> `869.900F` for readability, quote the YAML value: `"869.900"`.

### Parameter reference

| YAML key                | `#define` in `src/config.h` | Type    | Notes                                   |
|-------------------------|-----------------------------|---------|-----------------------------------------|
| `lora_frequency`        | `LORA_FREQUENCY`            | floatF  | Carrier MHz, emitted with `F` suffix    |
| `lora_spreading_factor` | `LORA_SPREADING_FACTOR`     | uint    | SF7..SF12                               |
| `lora_bandwidth`        | `LORA_BANDWIDTH`            | float   | 125.0, 250.0, 500.0 kHz                 |
| `lora_coding_rate`      | `LORA_CODING_RATE`          | uint    | 5..8 (4/5..4/8)                         |
| `lora_power`            | `LORA_POWER`                | int     | TX power in dBm                         |
| `lora_sync_word`        | `LORA_SYNC_WORD`            | uint    | Network identifier 0..255               |
| `lora_manager_id`       | `LORA_MANAGER_ID`           | hex16   | Mesh node ID, written as `0xNNNN`       |
| `wifi_ssid`             | `WIFI_SSID`                 | str     | Rendered as C string                    |
| `wifi_password`         | `WIFI_PASSWORD`             | str     |                                         |
| `mqtt_server`           | `MQTT_SERVER`               | str     | Broker host/IP                          |
| `mqtt_port`             | `MQTT_PORT`                 | int     |                                         |
| `mqtt_username`         | `MQTT_USERNAME`             | str     |                                         |
| `mqtt_password`         | `MQTT_PASSWORD`             | str     |                                         |
| `mqtt_topic_sub`        | `MQTT_TOPIC_SUB`            | str     |                                         |
| `mqtt_topic_out`        | `MQTT_TOPIC_OUT`            | str     |                                         |

To add a new parameter, extend `PARAMS` in
`scripts/testbed/configtool/schema.py` and update this table.

### End-to-end: "run an experiment on all devices"

```bash
# 1. Create your experiment YAML from the sanitised template.
cp scripts/testbed/experiments/example-baseline.yaml \
   scripts/testbed/experiments/current.yaml
$EDITOR scripts/testbed/experiments/current.yaml    # fill in credentials

# 2. Deploy everything — stop, upgrade, push configs via scp, upload + monitor.
./scripts/testbed/deploy.sh all -n my-run
```

That's it. `deploy.sh all` runs four phases:

1. **stop-monitor** — kill any existing monitors.
2. **upgrade** — `git pull` + `pio pkg update` on every gateway (brings
   infra changes only; experiment YAMLs are gitignored).
3. **push-config** — validates `current.yaml`, generates
   `scripts/testbed/change-config/change-config-{SHORT_ID}.sh` locally,
   scp's only the relevant scripts to each gateway's matching path.
4. **upload + monitor** — normal compile/flash; `gw-upload.sh` picks up
   the scripts scp'd in phase 3.

`./deploy.sh upload -n my-run` does the same thing without the stop/upgrade
phases; it also runs push-config first unless you pass `--skip-config`.

### Pointing at a different YAML

Default path is `scripts/testbed/experiments/current.yaml`. Override per
invocation with `-x`:

```bash
./scripts/testbed/deploy.sh all -n my-run -x scripts/testbed/experiments/exp-001.yaml
```

If neither `-x` nor `current.yaml` exists, push-config is a no-op with a
warning and the upload proceeds with whatever's already on the gateway —
useful when you have hand-written `change-config-*.sh` in the repo root.

Convention for switching between long-running experiments: keep real YAMLs
named descriptively (`baseline-2026-04-21.yaml`, `exp-001-sf7.yaml`, ...)
and symlink `current.yaml` at the active one:

```bash
ln -sf exp-001-sf7.yaml scripts/testbed/experiments/current.yaml
./scripts/testbed/deploy.sh all -n exp-001-sf7
```

### Individual configtool commands

You mostly won't need these — `deploy.sh` wraps them — but they're
available for debugging or ad-hoc workflows:

```bash
# Snapshot current state of the whole testbed (SSHes into every gateway)
# into experiments/baseline-YYYY-MM-DD.yaml.
python scripts/testbed/configtool/configtool.py harvest

# Semantic diff between two experiments (exit 1 if different).
python scripts/testbed/configtool/configtool.py diff \
    scripts/testbed/experiments/baseline-2026-04-21.yaml \
    scripts/testbed/experiments/current.yaml

# Schema-check a YAML without flashing (the same check deploy.sh runs first).
python scripts/testbed/configtool/configtool.py validate \
    scripts/testbed/experiments/current.yaml --strict-devices

# Generate scripts locally without pushing (inspect before flashing).
python scripts/testbed/configtool/configtool.py generate \
    scripts/testbed/experiments/current.yaml

# Push configs to gateways without flashing (useful for "prime and stage"
# flows). deploy.sh push-config = validate + generate + scp in one shot.
./scripts/testbed/deploy.sh push-config -x scripts/testbed/experiments/current.yaml
```

The generated scripts live in `scripts/testbed/change-config/`. They are
reproducible from the YAML; hand-edits are overwritten on the next
`generate`. If you need to keep a hand-written script, put it in the repo
root instead — `gw-upload.sh` prefers repo-root scripts over the generator
directory.

Details on internals, schema, and how to add a new parameter: see
`scripts/testbed/configtool/README.md`.

## Quick Start

```bash
cd scripts/testbed/
chmod +x deploy.sh gw-upgrade.sh gw-upload.sh gw-monitor.sh

# 1. Check all gateways are reachable
./deploy.sh status

# 2. Full deployment: upgrade + compile + upload + monitor
./deploy.sh all -n my-experiment

# 3. Collect logs later
./deploy.sh logs -n my-experiment

# 4. Stop monitors when done
./deploy.sh stop-monitor
```

## Command Reference

### `status` — Check connectivity and running processes

Shows gateway reachability, hostname, uptime, and any running `pio` processes.

```bash
./deploy.sh status              # All gateways
./deploy.sh status -g GW-1,GW-3  # Specific gateways only
```

### `upgrade` — Update code and libraries

Runs `git pull` + `pio pkg update` on all gateways in parallel.

```bash
./deploy.sh upgrade
```

### `upload` — Compile and flash firmware

For each device: runs `change-config-{SHORT_ID}.sh` then `pio run --target upload`.
Before uploading for a new experiment, make sure the change-config scripts
are generated from your experiment YAML — see [Experiment configs](#experiment-configs).

```bash
./deploy.sh upload -n cap2-v2                   # All devices
./deploy.sh upload -e ttgo-t-beam-v2            # Specific PIO environment
./deploy.sh upload -g GW-6                      # Only GW-6 devices
./deploy.sh upload -d GV-DD58,GV-14A4           # Specific devices only
./deploy.sh upload --skip-compile               # Upload already-built firmware
./deploy.sh upload -g GW-1 --skip-compile       # Re-flash GW-1 without recompiling
```

### `upload-monitor` — Upload + immediate monitor

Same as `upload` but starts a monitor for each device **immediately after upload**, capturing boot/init output that would be missed with a separate `monitor` step.

```bash
./deploy.sh upload-monitor -n cap2-v2           # Upload + monitor all
./deploy.sh upload --monitor -n cap2-v2         # Same thing with flag
./deploy.sh upload-monitor -n test -g GW-1      # Single gateway
```

### `monitor` — Start serial monitors

Starts `pio device monitor` in background for each device, logging to files. Stops any existing monitors first to free serial ports.

```bash
./deploy.sh monitor -n cap2-v2           # Named session
./deploy.sh monitor -g GW-1 -n test      # Monitor only GW-1 devices
```

Monitors run in the background on the gateways — `deploy.sh` exits after launching them. Log files are written on each gateway at:
```
~/LoRaChat/logs/{SESSION}/monitor-dev-{SESSION}-{SHORT_ID}.log
```

Each log line is prefixed with a full timestamp: `[2026-03-23 14:30:05] ...`

Example: `~/LoRaChat/logs/cap2-v2/monitor-dev-cap2-v2-E464.log`

### `stop-monitor` — Kill all monitors

Stops all running monitors and **auto-compresses** log files with gzip.

```bash
./deploy.sh stop-monitor                # All gateways
./deploy.sh stop-monitor -g GW-1       # Only GW-1
```

### `logs` — Collect logs locally

Copies log files from all gateways to your local machine.

```bash
./deploy.sh logs -n cap2-v2             # Specific session
```

Logs are collected into `./logs_testbed/{SESSION}/`.

### `clean` — Remove old logs

Deletes logs older than `MAX_LOG_AGE_DAYS` (default: 7) both locally and on all gateways.

```bash
./deploy.sh clean                       # Clean logs older than 7 days
```

### `sync-time` — Check and sync gateway clocks

Checks the time offset between your machine and each gateway. Attempts to enable NTP if possible.

```bash
./deploy.sh sync-time

# Output:
# Local time: 2026-03-23 18:30:05
#
#   GW-1  OK (offset: 0s)
#   GW-2  OK (offset: -1s)
#   GW-3  OFFSET: +15s — attempting sync...
#   GW-4  OK (offset: 0s)
```

If sync fails (no sudo), it suggests commands for your admin to run.

### `run-remote` — Execute command on all gateways

Runs any command on all (or filtered) gateways in parallel. Output is displayed per-gateway after completion.

```bash
# View a file on all gateways
./deploy.sh run-remote "cat /home/lora/LoRaChat/common-config-v2.sh"

# Update a config value
./deploy.sh run-remote "sed -i 's/LORA_MANAGER_ID=0xE646/LORA_MANAGER_ID=0x006C/' /home/lora/LoRaChat/common-config-v2.sh"

# Run on specific gateways only
./deploy.sh run-remote -g GW-1,GW-2 "ls /home/lora/LoRaChat/"

# Check disk space
./deploy.sh run-remote "df -h"
```

### `all` — Full deployment pipeline

Runs upgrade -> upload+monitor. Monitors start immediately after each device upload to capture boot output.

```bash
./deploy.sh all -n cap2-v2
./deploy.sh all -n cap2-v2 -e ttgo-t-beam-v2 -g GW-1,GW-2
```

## Device Map

| #  | Node          | UID | GW   | Location | ID hex | ID dec | Obs |
|----|---------------|-----|------|----------|--------|--------|-----|
|  1 | C6213-006C    |   0 | GW-2 | C6213    | 006C   |    108 | p2p |
|  2 | C6E104-E464   |   1 | GW-1 | C6E104   | E464   |  58468 |     |
|  3 | C6213-7B6C    |   2 | GW-2 | C6213    | 7B6C   |  31596 |     |
|  4 | GV-DD58       |   3 | GW-5 | GV-int   | DD58   |  56664 |     |
|  5 | C6E206-CB34   |   4 | GW-3 | C6E206   | CB34   |  52020 |     |
|  6 | C6E206-C270   |   5 | GW-3 | C6E206   | C270   |  49776 | X   |
|  7 | C6E104-77A4   |   6 | GW-1 | C6E104   | 77A4   |  30628 |     |
|  8 | C6E104-DF10   |   9 | GW-1 | C6E104   | DF10   |  57104 |     |
|  9 | C6E208-2A9C   |  10 | GW-4 | C6E208   | 2A9C   |  10908 |     |
| 10 | C6E208-1484   |  11 | GW-4 | C6E208   | 1484   |   5252 |     |
| 11 | C6E208-2AD4   |  12 | GW-4 | C6E208   | 2AD4   |  10964 |     |
| 12 | GV-3428       |  13 | GW-6 | GV-ext   | 3428   |  13352 | p2p |
| 13 | GV-14A4       |  20 | GW-5 | GV-int   | 14A4   |   5284 |     |
| 14 | D6105-7984    |  21 | GW-7 | D6105    | 7984   |  31108 |     |
| 15 | D6105-B4DC    |  22 | GW-7 | D6105    | B4DC   |  46300 |     |
| 16 | D6105-DD3C    |  23 | GW-7 | D6105    | DD3C   |  56636 |     |

## Flags Summary

| Flag | Description | Example |
|------|-------------|---------|
| `-e ENV` | PIO environment | `-e ttgo-t-beam-v2` |
| `-g GW-ID,...` | Limit to gateways | `-g GW-1,GW-3` |
| `-d DEV,...` | Limit to devices | `-d GV-DD58,GV-14A4` |
| `-n SESSION` | Session name | `-n cap2-v2` |
| `--skip-compile` | Upload only (skip build) | `--skip-compile` |
| `--monitor` | Start monitors after upload | `upload --monitor -n test` |
| `-c FILE` | Config file path | `-c /path/to/testbed.conf` |

## Configuration Reference

Key settings in `testbed.conf`:

| Setting | Default | Description |
|---------|---------|-------------|
| `DEFAULT_ENV` | `ttgo-t-beam-v2` | PlatformIO build environment |
| `GIT_BRANCH` | `new_loramesher` | Branch to pull on gateways |
| `REPO_PATH` | `/home/lora/LoRaChat` | Repo path on gateways |
| `PIO_PATH` | `/home/lora/platformio/bin` | PlatformIO bin dir (added to PATH for SSH) |
| `SERIAL_PORT_PREFIX` | `/home/lora/dev/lora-` | Serial port symlink prefix |
| `SSH_RETRIES` | `3` | SSH connection retry attempts (5s delay between) |
| `SSH_OPTS` | `ConnectTimeout=30 ...` | SSH connection options |
| `MAX_LOG_AGE_DAYS` | `7` | Log retention for `clean` command |
| `LOCAL_LOG_DIR` | `./logs_testbed` | Local directory for collected logs |

## Typical Workflows

### First-time setup
```bash
# 1. Edit testbed.conf with your gateway IPs and serial ports
vim testbed.conf

# 2. Make scripts executable
chmod +x *.sh

# 3. Verify connectivity
./deploy.sh status

# 4. Deploy everything
./deploy.sh all -n first-test
```

### Daily experiment
```bash
# Deploy and start monitoring (captures boot output)
./deploy.sh upgrade
./deploy.sh upload-monitor -n cap3-v2

# ... run experiment ...

# Collect logs and stop (logs are auto-compressed)
./deploy.sh logs -n cap3-v2
./deploy.sh stop-monitor

# Clean up old logs periodically
./deploy.sh clean
```

### Fix a single device
```bash
# Re-upload just one device with immediate monitoring
./deploy.sh upload-monitor -d GV-DD58 -n fix-dd58

# Or re-flash without recompiling
./deploy.sh upload -d GV-DD58 --skip-compile
```

### Re-flash after USB disconnect
```bash
# Skip compile, just re-upload to the affected gateway
./deploy.sh upload -g GW-3 --skip-compile
```

### Tail logs in real-time
```bash
# While monitors are running, tail a specific device log
ssh lora@gw-ip "tail -f ~/LoRaChat/logs/cap3-v2/monitor-dev-cap3-v2-E464.log"
```

## Serial Port Setup

The deploy scripts expect each ESP32 device to be accessible via a stable symlink at:

```
/home/lora/dev/lora-{SHORT_ID}
```

For example, device `C6E104-77A4` → `/home/lora/dev/lora-77A4` pointing to its actual `/dev/ttyUSBx`.

This is necessary because `/dev/ttyUSB0`, `/dev/ttyUSB1`, etc. can renumber after reboots or USB reconnects. Symlinks give each device a stable, predictable path.

### Option A: Manual symlinks (quick setup)

1. **Identify which `/dev/ttyUSBx` is which device.** Plug in one device at a time and check:
   ```bash
   ls /dev/ttyUSB*
   ```
   Or with all plugged in, check the kernel log:
   ```bash
   dmesg | grep ttyUSB
   ```

2. **Create the symlink directory:**
   ```bash
   mkdir -p /home/lora/dev
   ```

3. **Create symlinks for each device:**
   ```bash
   ln -sf /dev/ttyUSB0 /home/lora/dev/lora-E464
   ln -sf /dev/ttyUSB1 /home/lora/dev/lora-77A4
   ln -sf /dev/ttyUSB2 /home/lora/dev/lora-DF10
   ```

   > **Note:** These symlinks will break if USB ports renumber after a reboot. Use udev rules (Option B) for persistent mapping.

### Option B: udev rules (persistent, survives reboots)

udev rules automatically create symlinks when a device is plugged in, based on its USB serial number.

1. **Find the USB serial number of each device:**
   ```bash
   udevadm info -a -n /dev/ttyUSB0 | grep '{serial}'
   ```
   Look for the `ATTRS{serial}` value (e.g., `01234567`). Each ESP32 USB-UART chip has a unique serial.

2. **Create a udev rules file on the gateway:**
   ```bash
   sudo nano /etc/udev/rules.d/99-lora-devices.rules
   ```

3. **Add a rule for each device** (replace serial numbers with your actual values):
   ```
   # Device E464 (C6E104-E464)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{serial}=="01234567", SYMLINK+="lora-E464", MODE="0666"

   # Device 77A4 (C6E104-77A4)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{serial}=="89abcdef", SYMLINK+="lora-77A4", MODE="0666"

   # Device DF10 (C6E104-DF10)
   SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{serial}=="fedcba98", SYMLINK+="lora-DF10", MODE="0666"
   ```

   Common USB-UART vendor IDs:
   - `10c4` — Silicon Labs CP210x (most T-BEAM boards)
   - `1a86` — CH340/CH341
   - `0403` — FTDI

   > **Tip:** To find the right vendor ID: `udevadm info -a -n /dev/ttyUSB0 | grep idVendor`

4. **Reload udev rules and trigger:**
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

5. **If using `/home/lora/dev/` instead of `/dev/`**, adjust the symlink target. By default udev creates symlinks under `/dev/`. To use `/home/lora/dev/`, either:
   - Change `SERIAL_PORT_PREFIX` in `testbed.conf` to `/dev/lora-` and use the udev rules above as-is, or
   - Create a wrapper symlink:
     ```bash
     mkdir -p /home/lora/dev
     ln -sf /dev/lora-E464 /home/lora/dev/lora-E464
     # repeat for each device...
     ```

6. **Verify the symlinks work:**
   ```bash
   ls -la /dev/lora-* /home/lora/dev/lora-*
   ```

### Verifying from your machine

After setting up symlinks on all gateways, verify remotely:
```bash
# Check a single gateway
ssh lora@10.139.40.20 "ls -la /home/lora/dev/lora-*"

# Or check all gateways at once
./deploy.sh status
```

## Troubleshooting

### SSH connection refused / timeout
- Verify the gateway IP in `testbed.conf`
- If using passwords: ensure `sshpass` is installed (`apt install sshpass`) and `GW_PASS` is set
- If using SSH keys: check with `ssh -v user@gw-ip`
- For gateways with custom ports: set `GW_PORT` in `testbed.conf`

### SSH Permission denied (publickey)
- If the gateway uses key-based auth, set the key path in `GW_KEY`:
  ```bash
  declare -A GW_KEY=(
      [GW-7]="/home/jan/.ssh/id_rsa"
  )
  ```
- If the key has a **passphrase**, use `ssh-agent` to avoid interactive prompts:
  ```bash
  eval $(ssh-agent)
  ssh-add /home/jan/.ssh/id_rsa    # Enter passphrase once
  # Now deploy.sh works without prompting
  ```
- To make `ssh-agent` persistent across terminal sessions, add to `~/.bashrc`:
  ```bash
  if [ -z "$SSH_AUTH_SOCK" ]; then
      eval $(ssh-agent) > /dev/null
  fi
  ```
  Then run `ssh-add` once after each login.
- Make sure the gateway has no password set in `GW_PASS` (leave empty or remove the entry), otherwise `sshpass` will be used instead of the key

### Serial port not found
- Verify the symlink exists: `ssh lora@gw-ip "ls -la /home/lora/dev/lora-*"`
- The port path is auto-generated from the device ID: `C6E104-77A4` → `/home/lora/dev/lora-77A4`
- If the symlink naming differs, update `SERIAL_PORT_PREFIX` in `testbed.conf`
- Check permissions: user must be in `dialout` group

### PlatformIO lock error
- Another `pio` process is running on the gateway. Wait or kill it:
  `ssh user@gw-ip "pkill -f 'pio run'"`

### change-config script not found
- Scripts use the SHORT_ID (last 4 hex chars): `change-config-7B6C.sh`, not `change-config-C6213-7B6C.sh`
- `gw-upload.sh` searches in order: repo root → `scripts/testbed/change-config/` (scp'd by `deploy.sh push-config`) → `scripts/testbed/`
- If the YAML workflow isn't pushing to the gateway, confirm push-config actually ran: `./deploy.sh push-config -x <yaml>` will print "pushed N script(s)" per gateway. Then verify on the gateway: `ssh lora@gw "ls ~/LoRaChat/scripts/testbed/change-config/"`

### configtool harvest: SSH/scp failures
- Same auth path as `deploy.sh` — if `deploy.sh status` works for a gateway, harvest should too
- `WARN: scp config.h failed` and `note: no change-config-{SHORT_ID}.sh found` are informational; harvest continues and records whatever it could fetch. Missing change-config scripts just mean that device falls back to the gateway's `src/config.h` values
- Password auth still needs `sshpass` installed (`apt install sshpass`)

### configtool validate: schema errors
- `unknown parameter 'lora_foo'` — typo, or the parameter isn't in the schema; check the [parameter reference](#parameter-reference) table
- `unknown top-level key` — only `experiment`, `description`, `defaults`, `devices` are allowed at the root
- `device 'XXXX' not in testbed.conf DEVICES` (only with `--strict-devices`) — the YAML references a device that isn't in `testbed.conf`; add it there or remove from the YAML

### Generated change-config script got hand-edited
- The YAML is the source of truth; re-running `deploy.sh push-config` (or `configtool generate`) overwrites the script
- To keep a hand-written script for one device, move it to the repo root — `gw-upload.sh` checks the repo root first and will prefer your hand-written version

### push-config fails with "device 'XXXX' not in testbed.conf DEVICES"
- `deploy.sh push-config` runs `validate --strict-devices`, which requires every YAML device to be in `testbed.conf` `DEVICES`. Either add the device to `testbed.conf` or remove it from the YAML. If you want to deploy to a subset, use `deploy.sh upload -g GW-X` rather than editing the YAML.

### Upload happened but the device still has old values
- Confirm push-config wasn't skipped. The upload step auto-runs it unless `--skip-config` is passed. If you passed that flag, run `./deploy.sh push-config` manually
- On the gateway, check the pushed file: `ssh lora@gw "cat ~/LoRaChat/scripts/testbed/change-config/change-config-XXXX.sh"` — does it show the values you expect?
- Boot log: after flashing, the device prints its `LORA_*` values early in boot. Grep the session monitor log for `LORA_POWER` and similar to confirm what actually ran

### Accidentally committed testbed.conf with real passwords
- Run `git update-index --skip-worktree scripts/testbed/testbed.conf` to prevent future accidents
- If the commit hasn't been pushed, `git reset HEAD~` + edit + re-commit with placeholders
- If it has been pushed, rotate the leaked passwords and scrub history (`git filter-repo` or equivalent)

### Monitor dies after SSH disconnect
- Monitors use `nohup` so they should survive SSH disconnects
- Check with: `./deploy.sh status` then SSH in manually to verify
- If monitors stop, restart with: `./deploy.sh monitor -n same-session`

### Logs not collected
- Verify the session name matches: `ssh user@gw-ip "ls ~/LoRaChat/logs/"`
- Check that monitors have been running (empty log = monitor never started)
