# Energy bench with the Nordic PPK2

How to measure a T-Beam node's current with a Nordic Power Profiler Kit II (PPK2), which tests to run and how to analyse the traces. The firmware has no MCU sleep yet (`CONFIG_PM_ENABLE` off, 160 MHz), so these numbers are the baseline that light sleep will be compared against.

## Wiring (T-Beam v1.1 AXP192 and v1.2 AXP2101)

```
PPK2 (source meter, 5000 mV)          T-Beam header
VOUT  ─────────────────────────────▶  5V
GND   ─────────────────────────────▶  GND
VIN   (not connected)
USB "DATA/POWER" ──▶ PC (a USB 3 port if possible)
USB "POWER ONLY" ──▶ a second PC port (for the WiFi tests)
T-Beam USB ──▶ PC only through a VBUS-cut cable
```

The header `5V` pin is the USB 5 V input of the power chip (AXP), so the whole board is measured: the AXP and its converters, the ESP32, the radio (LDO2/ALDO2) and the OLED. This is the **input path**, the way a solar node runs in daytime and a mains gateway always runs. It differs from running on the battery in three ways:
- the radio's linear regulator drops from 5 V;
- the USB-serial chip is powered too;
- the firmware sees external power (`/power` shows `vbus=yes`), so OTA has no battery limit.

For **battery life**, run E3-BAT once per board revision:
- PPK2 VOUT goes to the 18650 holder `+` tab and GND to `−`, at 3.8 V. Use soldered wires or a dummy cell.
- Disconnect the 5V-pin wire.
- Check the polarity with a multimeter first: assume there is no reverse-polarity protection.
- E3-BAT / E3 is the ratio that converts input-path power into battery-path power (`ppk2_report.py --battery-ratio`).

Never feed the header `3V3` pin. It is the AXP's DCDC1 output:
- on v1.1 it feeds only the OLED and the header;
- the radio rail stays off unless the AXP has a 5 V input or a battery.

Rules:
- 5.0 V is the PPK2 maximum. Use short, thick wires (under 20 cm): the AXP limits its input near 4.4 V, and WiFi peaks of 300–400 mA cause a voltage drop along the wire.
- Remove the 18650 cell for 5V-pin runs, or the AXP charges it and the trace shows the charging current.
- Never plug in a normal USB cable while the PPK2 powers the board: it ties the PC's 5 V onto the PPK2 output.
- **VBUS-cut cable:** check it with a multimeter (no continuity on VBUS). It keeps serial and flashing working; the USB-serial chip's current is then part of the reading.
- The PPK2 measures in the positive line, so the shared ground through the PC does not matter.
- Keep the LoRa antenna fitted.

**Second USB port on the PPK2:** one PC port is enough for the mesh-only tests (about 30–60 mA). The WiFi tests peak at about 250–350 mA, which gets close to a USB 2.0 port's 500 mA. A PC port can't be damaged by this; the worst case is the PPK2 dropping out and that test being lost.

## Recording

### nRF Connect for Desktop, Power Profiler app

1. Mode **Source meter**, supply **5000 mV** (3800 mV on the holder), then **Enable power output**.
2. Set the log folder (the app's root directory) to `release\bench\energy\ppk2`. `release/` is ignored by git.
   - Each session writes `<session id>/session.raw` there: float32 current in µA plus a uint16 of digital bits per sample, at 100 kS/s.
   - `ppk2_report.py` reads that file directly, so no export is needed.
   - Keep the "disk full" trigger (4096 MB). 100 kS/s is 600 KB/s, about 2.2 GB per hour.
3. A CSV export (**Export**, timestamp + current) also works. Name it `<date>_<board>_<test>_<voltage>.csv`, e.g. `2026-10-13_COM3_E3_5v0.csv`; the voltage is read from the name.
4. Note the PC time at **Start** and every manual action (button press, gateway unplugged) in `notes.txt` next to the recording.
5. Close the app before `ppk2_bench.py` runs: only one program can open the PPK2.

### `scripts/energy/ppk2_bench.py` (Windows Python)

It drives the PPK2 (`ppk2-api`, installed in the PlatformIO penv) and records the node's and the gateway's serial logs on the same PC clock:

```
python scripts\energy\ppk2_bench.py --test E3 --board COM3 --seconds 900 ^
    --node-port COM3 --gateway-port COM7 [--power-cycle 5] [--raw] ^
    [--at "30:@5E9C /displayOn"] [--every "30:@5E9C /version"]
```

- `--power-cycle S` switches the board off for S seconds and starts recording before switching it on, so boot and the 60 s boot window are in the trace.
- `--at` and `--every` send lines to the gateway console. Signed lines (e.g. `/maint.open`) come from `scripts/ota_tools/lmcmd.py sign`.
- Output goes to `release/bench/energy/<date>_<board>_<test>_<voltage>/`:
  - `trace.csv`: 1 ms mean and max bins;
  - `node.log` and `gateway.log`: `<unix time>\t<line>`;
  - `meta.json`, which holds `epoch`, the unix time of the first sample;
  - `raw.f32` with `--raw`: every sample.
- The `ppk2-api` library is GPL-2.0. It is a host tool only and never goes into the firmware.

## Analysis: `scripts/energy/ppk2_report.py` (WSL, numpy/pandas)

```
python3 scripts/energy/ppk2_report.py release/bench/energy/ppk2/<session id>/session.raw --voltage 5.0 --start 120
python3 scripts/energy/ppk2_report.py release/bench/energy/2026-10-13_COM3_E3_5v0
```

It writes `<name>.json` and `<name>.png` with:
- the average, median, 95th percentile and peak current;
- charge (mAh), energy (mWh) and average power;
- the bursts above median + `--threshold-ma` (LoRa TX, WiFi) and their extra charge;
- the superframe period (autocorrelation) and the mean current over one superframe;
- the battery life of a 3000 mAh, 3.7 V cell with 80 % usable and no solar (`--cell-mah`, `--cell-v`, `--usable`, `--battery-ratio`).

`--start` and `--end` select a window, e.g. the steady state after boot.

## Tests

Setup: the sensor under test on the PPK2; the gateway on its own USB as mesh manager; the bench hotspot and bench server up; the display asleep unless the test says otherwise.

| Test | What | Trigger | Duration | Result |
|---|---|---|---|---|
| E0 | Wiring check | output on | 3 min | boots, joins (gateway `/getRT`), no clipping; USB-serial chip share (cable in vs out) |
| E8 | OTA update | power-on → boot window pulls a newer image | ~4 min | energy per update: download, flash write, reboot, self-test |
| E2 | Boot, no update | `--power-cycle 5` | 3 min | energy per power-on (60 s WiFi boot window, 60 s OLED) |
| E2b | Boot without hotspot | hotspot off | 3 min | worst-case boot |
| E3 | Steady state, joined | after E2 | 15 min | average power, superframe profile |
| E3-BAT | Steady state, battery path | holder, 3.8 V | 15 min | battery ratio, battery life |
| E4 | Gateway gone | unplug the gateway | 10 min | current while searching for a network |
| E5 | Display | `--at "30:@<node> /displayOn"` | 2 min | energy per display wake |
| E6 | Commands | `--every "30:@<node> /version"` | 10 min | charge per request/reply |
| E7 | Maintenance window | signed `@<node> /maint.open 120` | 3 min | cost of a WiFi window |
| E1 | Off | hold PWR 4 s (holder, 3.8 V) | 5 min | drain while switched off |

## Results

Recorded in `todo.md` (energy section).
