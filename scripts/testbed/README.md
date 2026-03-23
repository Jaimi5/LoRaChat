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

5. **change-config scripts** present. Each device needs a `change-config-{DEVICE_ID}.sh` in the repo root that modifies `src/config.h` (WiFi SSID, MQTT ID, NM_ID, LoRa power, SF, etc.)

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

### `all` — Full deployment pipeline

Runs upgrade -> upload -> monitor in sequence. Prompts to continue if any phase fails.

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

**Not included** (GW-7 currently unavailable):
- D6105-7984 (UID 21), D6105-B4DC (UID 22), D6105-DD3C (UID 23)

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
- Ensure the script exists in `~/LoRaChat/` on the gateway
- The script is searched in: repo root, then `scripts/testbed/`

### Monitor dies after SSH disconnect
- Monitors use `nohup` so they should survive SSH disconnects
- Check with: `./deploy.sh status` then SSH in manually to verify
- If monitors stop, restart with: `./deploy.sh monitor -n same-session`

### Logs not collected
- Verify the session name matches: `ssh user@gw-ip "ls ~/LoRaChat/logs/"`
- Check that monitors have been running (empty log = monitor never started)
