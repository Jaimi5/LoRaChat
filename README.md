# LoRaChat Firmware

LoRaChat is ESP32 firmware for LoRa mesh networks of sensor nodes. It runs on TTGO T-Beam boards and uses [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) 2.0, a TDMA LoRa mesh protocol, to carry application messages between nodes. A gateway node bridges the mesh to an MQTT broker over WiFi. Firmware updates are installed over the air with automatic rollback.

You can join LoRaMesher's developers and users on **[Discord](https://discord.gg/KCY78MbpUa)**, if you like.

## Organizations & Contributors Using This Project

- [Hacking Ecology (Nayad)](https://hackingecology.com/) - Hacking Ecology transforms water monitoring with accessible and open-source systems.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/enclosure%20logo.png" height="60">](https://hackingecology.com/)

- [Universitat Politècnica de Catalunya](https://www.upc.edu/) and [CNDS research group](https://www.ac.upc.edu/en/research/research-groups/cnds) - The group investigates AI/ML in the IoT, Community Networks, and LoRa Mesh Networks.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/upc-positiu-p3005-interior-blanc.png" height="60">](https://www.upc.edu/)


## Demonstrator

A live demonstration of an operation LoRaChat application is [here](https://tomir.ac.upc.edu/loraupc/index.php).

## Supported hardware

One firmware image runs on both TTGO T-Beam revisions:

- T-Beam v1.0/v1.1 (AXP192 power management)
- T-Beam v1.2 (AXP2101 power management)

The power management chip is detected at boot. The firmware powers the LoRa radio, the ESP32 and the rail of the OLED and the 3.3 V header pins, and switches the GPS off (it is not used). Batteries are charged to 4.2 V. The user LED (GPIO4) is kept off except during `/identify`. With a battery fitted, the charge LED is driven by the charger: on while charging, off when full or on battery, blinking on a charge fault; without a battery it stays off (the T-Beam v1.2 reports battery insertion and removal, and the LED follows). The charger is restarted at every boot, which clears a latched charge fault as unplugging USB does. `/power` prints supply, battery and charger state (an AXP192 without a battery reports a phantom cell at about 3.3 V that does not charge).

### Environment sensor (optional)

A node can carry a Bosch BME280 (temperature, pressure and humidity) or BMP280 (temperature and pressure) on a GY-BME280 / GY-BMP280 3.3 V breakout. Sellers often mix the two chips up, so the firmware reads the chip id and handles both (ids 0x60, 0x58, 0x56 and 0x57) at I2C address 0x76 or 0x77.

| Breakout | T-Beam header |
|---|---|
| VIN (VCC) | 3V3. Never 5V: this breakout has no regulator and no level shifter |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

The sensor shares the bus with the power chip (0x34) and the OLED (0x3C). The node looks for it once at boot and logs one line with the result. A board without a sensor works the same. The chip sleeps between measurements (about 0.1 µA), and the node takes one measurement per monitor report, at ×1 oversampling. The reading travels in the monitor report, shows on the last row of the screen (`21.4C 61%RH` for a BME280, `21.4C 1013hPa` for a BMP280) and is printed by `/sensor`. A failed measurement is logged once and the report leaves without the reading.

A sensor inside a closed box measures the air in the box, which the sun and the electronics warm up. Mount it outside the box, in the shade, for weather data.

The 3.3 V pin stays powered in light sleep and in deep sleep, since the power chip keeps its rails on. The sensor is in its own sleep mode during both.

### Display and power button

The OLED shows one status screen for 60 s after boot (`DISPLAY_AWAKE_MS` in `src/config.h`) and then sleeps. After a power-on the logo comes first, for one second. The screen is redrawn once per second at most, and only when its content changed.

The power button (PWR):

- A short press turns the display on for 60 s. A press while it is on restarts the 60 s.
- Holding it for 4 s switches the board off. The power chip does this by itself.
- A sensor in light sleep (`/sleep.set light`) wakes at the press and lights the display at once. The node then stays awake while the display is on.
- A sensor in deep-sleep mode (`/sleep.set deep`) starts without the display after each deep sleep. The button does nothing until the next power-on or restart, and it does not end a deep sleep.

The screen of a sensor and of a gateway:

```
~ 0A3F JOINED   3.98V      G 0A00 GATEWAY    USB
GOOD   12s                 MQTT    3s
via 0B12 +6.5  2 hops      wifi -61dBm  nodes  5
0B12  -97dBm +6.5  4s      0B12  -97dBm +6.5  4s
0C40 -112dBm -8.0 12s      0C40 -112dBm -8.0 12s
0D07 -118dBm-11.5 40s      0D07 -118dBm-11.5 40s
21.4C 61%RH    v0.1.3      up 3d          v0.1.3
```

| Row | Content |
|---|---|
| 1 | Icon, node address, mesh state, power. `3.98V` is the battery, `+3.98V` a battery with USB or solar power connected, `USB` external power without a battery, `--` unknown |
| 2 (large) | Sensor: the link verdict and, on the right, how long ago the node last heard anything from the mesh (`--` when it has heard nothing since boot). While the node looks for a network: `SEARCH` or `JOIN` and the time spent. Gateway: `MQTT` and the same age when all is well. A gateway fault shows its word alone: `NO WIFI` or `NO MQTT` |
| 3 | Sensor: the neighbour that relays toward the gateway, the SNR of its last packet and the hops to the gateway (`1 hop`, `2 hops`). The `dB` is left out when the row is full. `no route` without a gateway, `no sync yet` before the first sync. In install mode the right side shows the time left as `m:ss`. Gateway: WiFi signal and the number of nodes in the mesh |
| 4 to 6 | The three direct neighbours with the strongest signal: address, RSSI, SNR and the age of their route |
| 7 | The last `/displayText` line for five minutes, otherwise the sensor reading, otherwise the uptime. The firmware version on the right |

Mesh states: `INIT`, `DISCOVER`, `JOINING`, `JOINED`, `MANAGER`, `RECOVER`, `ELECTION`. A healthy gateway shows `GATEWAY`, and a node in install mode shows `INSTALL`. The icon repeats the state: a dot with arcs when joined, a radar that grows once per second while searching, an arrow toward a dot while joining, a house on a gateway and an exclamation mark on a fault. In `RECOVER` and `ELECTION` the first row is inverted.

The verdict comes from the SNR of the neighbour toward the gateway, or of the best neighbour when the node has no route yet. Use it to place a node:

| Verdict | SNR | Meaning |
|---|---|---|
| `GOOD` | -2 dB or more | Leave the node here |
| `FAIR` | -7 dB to -2 dB | It works with about 5 dB of margin. Try a better spot if one is close |
| `POOR` | below -7 dB | Too close to the SF9 limit of about -12.5 dB. Move the node or add a relay |
| `NONE` | no reading | The node hears nobody yet |

Ages have one unit: `45s`, `4m`, `2h`, `9d`.

The age on the large row is the smallest of two times: the time since the last application message for this node arrived, and the smallest route age among its direct neighbours. LoRaMesher resets the route age of a node (`last_seen_ms`) whenever a packet it hears refreshes that route: a routing message from the node, a routing message from another neighbour that lists it, or a sync beacon of the network manager. A sensor that only sends therefore still shows a small age while its neighbours are in range.

### Placing a node

There are two ways to find a good spot:

- **At the node.** Press the power button and read the large row. Move the node, wait for the age to restart from a few seconds and read the verdict again. The display keeps the node awake while it is on.
- **From the web.** Send the signed command `/install` to the node (ten minutes by default) and watch its reports arrive. In install mode the node does not sleep and sends a monitor report every two superframes, at least 10 s apart. The superframe depends on the network (see [Commands for other nodes](#commands-for-other-nodes)). On a bench with 2 nodes, 2 data slots per node and a sleep fraction of 85 % it lasted 76.95 s, so the reports came every 153.9 s. The screen shows `INSTALL` and the time left. The mode ends by itself, after `/install 0` or at a reboot, and the node then reports and sleeps as before.

One install report is a 57 byte packet with three neighbours, about 0.46 s on air at SF9. With a short superframe of 15.2 s (2 nodes, one data slot each, no sleep fraction) a report leaves every 30.4 s. That is 1.5 % of the time, next to the 3.4 % of the node's own mesh traffic. A longer superframe sends fewer reports, so the node stays below the 10 % duty cycle of the 869.4 to 869.65 MHz band.

## Building

The project uses [PlatformIO](https://platformio.org/) with the Arduino framework as an ESP-IDF component (`espressif32@6.5.0`, Arduino 2.0.14, ESP-IDF 4.4.6).

| Environment | Use |
|---|---|
| `tbeam` | Release image for both T-Beam revisions |
| `tbeam-debug` | Same image with verbose logs, heap poisoning and heap tracing |
| `native` | Unit tests of the pure logic in `lib/` (GoogleTest) |

```bash
scripts/pio.sh run -e tbeam                       # build
scripts/pio.sh run -e tbeam -t upload -t monitor  # flash over USB and open the serial monitor
scripts/pio.sh test -e native                     # unit tests
python3 -m pytest scripts/ota_tools/tests test/host
scripts/pio.sh run -e tbeam -t idedata            # include paths for clang-tidy (no compile)
python3 scripts/static_check.py clang-tidy --build-dir <workspace>/build/tbeam
python3 scripts/static_check.py cppcheck
```

`scripts/pio.sh` wraps `pio`. Under WSL it keeps the PlatformIO workspace on the Linux filesystem, which makes builds several times faster than on a Windows drive.

The native tests compile the mbedTLS of the ESP-IDF package for the host, so build `tbeam` once before the first `scripts/pio.sh test -e native`. The host tools need Python 3 with `pip install -r scripts/ota_tools/requirements.txt`.

### Checks for memory and concurrency errors

- **Sanitizers.** The native tests run with AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer and the libstdc++ assertions. Any finding stops the program and fails the suite; the runtime options are compiled in from `test/sanitizerOptions.cpp`. PlatformIO prints only that the suite errored, so run the test program (`<workspace>/build/native/program`) directly to read the report. `test/host/test_native_sanitizers.py` checks that each class of error does fail a run.
- **Warnings.** `test/host/test_lib_cxx11.py` builds `lib/` as gnu++11 and gnu++17 with `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Werror`.
- **Static analysis.** `scripts/static_check.py` runs clang-tidy 18 (checks in `.clang-tidy`: the clang static analyzer with its leak and new/delete checks, smart-pointer ownership, use-after-move, thread-unsafe calls) and cppcheck over `src/` and `lib/`. clang-tidy needs the include paths of the tbeam build (`-t idedata`, which only configures). Both fail on any finding. Suppress a finding only inline, with the reason: `// NOLINT(<check>): <reason>` or `// cppcheck-suppress <id> ; <reason>`.
- **Formatting.** Lines changed by a commit must follow `.clang-format`: `git clang-format-18 --diff <base>`.

`<workspace>` is where `scripts/pio.sh` puts the PlatformIO build (`~/.cache/pio-ws/<folder>` under WSL, `.pio` elsewhere).

GitHub Actions (`.github/workflows/firmware.yml`) runs on every push:
- the rollback guards, the host tests and cppcheck
- the native tests with the sanitizers
- clang-tidy on the `tbeam` sources
- the format check of the changed lines
- a build of `tbeam` and `tbeam-debug` with every feature on
- the 90 % size gate
- the license and acknowledgements check (`scripts/ota_tools/third_party.py`)

LoRaMesher is pinned to one commit in `platformio.ini`, so CI builds the same library as the bench.

## Versions, releases and updates

- **Version:** `version.txt` (`MAJOR.MINOR.PATCH`). Every build writes it into the image with the git commit, e.g. `0.1.0+gabc1234`, and the node prints it at boot: `BOOT part=app0 state=VALID ver=0.1.0+gabc1234 ...`.
- **Release:** after a build, `python3 scripts/ota_tools/release.py --env tbeam --sign-key <key.pem> --key-id <id>` checks the image and writes `release/tbeam/<version>/` with the images, `info.json`, the signed `manifest.bin` and `bundle.bin`.
- **Keys:** images are signed with ECDSA P-256. Key id 1 is the test key in `test/vectors/test_key.pem` for bench and test images; production builds (`OTA_PRODUCTION`) refuse it. Production keys are created with `sign_manifest.py keygen` and kept offline; their public keys go into `src/ota/otaKeys.cpp`.
- **Rollback:** the flash holds two app slots. A newly installed image boots in `PENDING_VERIFY`; the boot guard (`src/ota/otaBootGuard.cpp`) runs a self-test (radio, power chip, heap, signature check, mesh contact) with a 240 s deadline and an RTC watchdog (timed on the chip's calibrated slow clock), and either keeps the image or makes the bootloader return to the previous one. An image that fails its self-test is blacklisted at once; one that crashes or hangs without a recorded reason is blacklisted after its second rollback. A release built with `--skip-mesh-check` passes without mesh contact (for a change of the mesh protocol). Every rollback is reported.
- **Upload through the node's access point (push):** `/maint.open <seconds> ap` opens the WPA2 access point `LM-<address>`; its password derives from the deployment key (`scripts/ota_tools/ap_pass.py show --key-file <key> --node <address>` prints it and the WiFi QR text). Join it and open `http://192.168.4.1/`, choose the release's `bundle.bin` and upload it. The node checks the signed manifest and the length before writing anything, then writes and checks the image like the pull and reboots into it.
- **Over-the-air download (pull):** in a maintenance window the node downloads `manifest.bin` from the OTA server (`OTA_SERVER_URL`), checks its signature and the update policy (board, partition table, size, version, blacklist, battery ≥ 3.6 V unless powered, heap), stops the mesh, streams `firmware.bin` into the other app slot with a running SHA-256 (3 attempts, each from byte 0), checks the written image against the manifest and reboots into it. The boot guard then keeps it or rolls back. After a failed download the node restarts its current image and skips that image until the next power-on. In each window the node reports to `/report` on the server's host (signed with a key derived from the deployment key): nothing to install, image written, download failed, and, at the next window after the reboot, whether the image was kept or rolled back. Command-opened windows: `/maint.open` below.

Commands for keys, signing, inspecting a manifest (`sign_manifest.py show`), test images and bench flashing are in [scripts/ota_tools/README.md](scripts/ota_tools/README.md).

## Configuration

Site settings are kept out of git. Copy `src/config_local.example.h` to `src/config_local.h` and set:

- `WIFI_SSID`, `WIFI_PASSWORD`: default WiFi network of the gateway. Credentials stored in NVS with the `/addSSID` and `/addPassword` commands take priority.
- `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USERNAME`, `MQTT_PASSWORD`: MQTT broker. MQTT is disabled when `MQTT_SERVER` is empty.
- `LORA_MANAGER_ID`: address of the node that acts as LoRaMesher network manager.
- `OTA_SERVER_URL`: folder with `manifest.bin` and `firmware.bin` for updates. Default `http://loramesher.com/fw/<environment>/`; on the bench the PC running `scripts/ota_tools/fw_server.py`. Empty disables the update check.

Product settings (features, LoRa radio parameters, monitor period) are in `src/config.h`, board pins in `src/boards/tbeam.h`. ESP-IDF options are in `sdkconfig.<environment>`.

## Node roles and the serial console

Every node runs the same image and has a role, stored in NVS:

- `gateway`: LoRaMesher network manager, with WiFi and MQTT always on. Normally on mains power.
- `sensor`: mesh node without WiFi or MQTT. Data reaches MQTT through the gateway.

A node without a stored role is the gateway if its mesh address equals `LORA_MANAGER_ID` (`config_local.h`), and a sensor otherwise.

### Commands

Text commands arrive from three channels and run one at a time:

- **USB serial** (115200 baud, one command per line): trusted, runs every command. The reply follows a `> <command>` line; the arguments are not echoed, since they can hold keys.
- **LoRa**: a line typed as `@<node> <command>` on a node's serial console is sent to that node; its reply arrives as `< <node> [<request>] <reply>`.
- **MQTT** (gateway): publish `<node> <request id> <command>` on `cmd/<gateway>`. The reply is published on `cmd-resp/<node>` as `<request id> <reply>`. A command for another node is sent on over LoRa, and the gateway reports its progress on `cmd-ack/<node>`.

Every command has a permission: **Open** commands run from any channel, **Signed** commands need a signature when they come over LoRa or MQTT, and **local** commands only run on the serial console. A signature is ` #<counter>.<tag>` at the end of the line, made with the deployment key by `scripts/ota_tools/lmcmd.py sign` (see [scripts/ota_tools/README.md](scripts/ota_tools/README.md)). A node runs each counter once. An older counter gets the error `replay`. The same counter again gets the stored reply with its stored status, or `OK already executed` if the node restarted in between.

#### Reply status

Every reply carries a status. 0 is success and the reply is the text of the command. 1 to 127 are the same errors for every command. 128 to 255 are errors of the command that was sent, and `/cmds` lists their texts. An error may carry one short argument.

| Code | Name | Argument | Meaning |
|---|---|---|---|
| 1 | unknown | | Unknown command |
| 2 | perm | | This command needs a signature |
| 3 | auth | | The signature is not valid |
| 4 | replay | | An older command than the last one. Not run |
| 5 | arg | | Bad argument |
| 6 | storage | | The node could not store the command counter |
| 7 | no route | | The gateway has no route to the node |
| 8 | timeout | seconds waited | No reply within the time the gateway waited |
| 9 | dropped | | Dropped by the gateway, too many commands waiting |
| 10 | too long | max line bytes | The command does not fit in one LoRa frame |
| 11 | busy | | The node cannot run this now |

Codes 7 to 10 come from the gateway and never travel over LoRa. On the serial console an error prints its text, and a bad argument prints `Usage: <command> <arguments>`. On MQTT an error is `<request id> E<code>` or `<request id> E<code> <argument>` on `cmd-resp/<node>`, for example `17 E7`, `17 E8 94` or `17 E5`. No successful reply starts with `E` and a digit. On LoRa a reply frame is the flags byte, the status byte and then the text or the argument.

`/cmds [page]` lists every command for programs. Ask the gateway itself over MQTT, so the answer does not cross LoRa. Each page stays under 900 bytes:

```
cmds 1 0.1.2+g2bfdeeb 1/4
E|1=unknown;2=perm;3=auth;4=replay;5=arg;6=storage;7=no route;8=timeout;9=dropped;10=too long;11=busy
/addPassword|local|<password of at most 64 bytes>|
/connectWiFi|signed||128=The WiFi did not connect
```

Line 1 holds the format version `1`, the firmware version and the page position. The `E|` line with the generic errors comes on page 1 only. Then one line per command, sorted by name: `name|perm|usage|code=text;code=text`, where perm is `open`, `signed` or `local`. A command name registered by two services is refused at boot with an error in the log.

#### Commands for other nodes

A gateway answers every command it sends on to another node. `E7` comes at once when the routing table of the gateway has no route to the node, and nothing is sent over LoRa then. A route is an entry that the mesh still uses. A direct neighbour that went silent is still tried while the mesh lists it, which lasts a few minutes. A silent node behind a relay has no route.

Otherwise the gateway waits as long as a reply without losses can take: `A = w + (h + 1) · S + ceil(P / S) · S + 2 s`. `S` is the superframe, `h` the hops to the node and `P` the time the command needs to run (0 for most, the measurement time for `/sensor`). `w` is the time until the gateway transmits the command. The gateway sends one message in each of its TX data slots. With `n` slots per superframe and `q` messages already in its queue, the command waits `floor(q / n)` superframes and then for the slot that is left to it. When the gateway cannot tell which of its slots comes next, the longer wait counts.

The gateway never sends a command again before `A`, because the first one may still be on its way. A signed command is sent once more after `A` and given up after another `A`, and the node still runs it once. An unsigned command is given up after `A` (`E8 <seconds>`). `E9` means that more than eight commands were waiting and the oldest gave up its place. A reply that arrives up to ten minutes after its `E8` is still published.

The superframe depends on the network: the number of nodes, the data slots per node (2 by default) and the sleep fraction stored on the gateway. Two measured cases, both with 2 nodes and slots of 950 ms: 15.2 s (16 slots) with one data slot per node and no sleep fraction, and 76.95 s (81 slots) with 2 data slots per node and a sleep fraction of 85 %. The current value is `superframe_ms` in every `Q` line and `sf_ms` in the monitor report. Example with the second case: a node one hop away and the gateway's slot 54 s away give `A` = 54 + 2 · 76.95 + 2 = 209.9 s. `A` is the limit. On that bench the reply came back in the same superframe, 2 to 3 s after the gateway's slot, so it arrived about `w` + 3 s after the command was published.

For a command from MQTT the gateway also publishes on `cmd-ack/<node>`:

| Line | When |
|---|---|
| `<request id> Q <giveup_s> <expected_s> <hops> <superframe_ms>` | The command was sent on. `giveup_s` is the time until `E8`, `expected_s` the time until the reply is expected |
| `<request id> R <giveup_s>` | The command was sent a second time. `giveup_s` is the time left |
| `<request id> D <rtt_ms> <attempts>` | Right after a reply was published. `rtt_ms` is the time from the first send to the reply, `attempts` is 1 or 2 |

`expected_s` comes from the replies the gateway has measured for that node (a smoothed round trip time as in RFC 6298, kept in RAM), or from `w + h · S` while it has none. The measurement never shortens a wait. The same command published again with the same request id while it waits is not sent again. The gateway answers it with a new `Q` line that holds the remaining times. Commands that the gateway runs itself and commands refused with `E7` or `E10` get no line on `cmd-ack`. The broker must let the gateway user write `cmd-ack/#`.

| Command | Permission | Effect |
|---|---|---|
| `/help` | Open | Lists the commands the channel may run |
| `/cmds [page]` | Open | Lists every command with its permission, arguments and errors, for programs |
| `/version` | Open | Version, slot, image state, bootloader hash, board and address |
| `/ota.status` | Open | Last update attempt and its outcome |
| `/role` | Open | The role and whether it is stored or the default |
| `/power` | Open | Power chip, supply, battery and charger state (with the AXP2101 registers) |
| `/sensor` | Open | The environment sensor (chip and I2C address) and a new measurement. Error 128 means that no sensor was found |
| `/identify [s]` | Open | Blinks the user LED and the charge LED for s seconds (default 10, from 1 to 600) to find the board. Any other value is a bad argument |
| `/install [seconds]` | Signed | Install mode for 600 s, or for the given time up to 3600 s: the node stays awake and reports every two superframes. A new `/install` sets a new end time, `/install 0` stops it. A value above 3600 is a bad argument. Replies `OK install <n> s` or `OK install off` |
| `/role.set gateway`, `/role.set sensor` | Signed | Stores the role and restarts the node. Refused while a newly installed image is still being verified |
| `/sleep` | Open | Sleep mode and sleep fraction (stored or default) and the light and deep sleeps since power-on |
| `/sleep.set none\|light\|deep` | Signed | Stores how a sensor's MCU sleeps through the SLEEP slots and restarts the node. Refused while a new image is being verified |
| `/sleep.fraction <0-90>` | Signed | Stores the minimum share of the superframe the network sleeps (used by the network manager, i.e. the gateway) and restarts the node |
| `/key` | Open | Whether a deployment key is stored, and its fingerprint (compare with `ap_pass.py show`) |
| `/key.set <64 hex digits>` | local | Stores the deployment key (created with `scripts/ota_tools/ap_pass.py keygen`) |
| `/maint.open <seconds>` | Signed | Sensor nodes: joins the WiFi network and checks the OTA server, up to 7200 s. On a gateway: checks the OTA server right away over its WiFi (MQTT stops while it installs) |
| `/maint.open <seconds> ap` | Signed | Sensor nodes: opens the access point `LM-<address>` for an upload from a phone or PC, up to 1800 s. Needs the deployment key |
| `/maint.close` | Signed | Closes the open maintenance window |
| `/maint.wifi …` | Signed | Stores the node's WiFi credentials (gateway WiFi and maintenance windows). Over LoRa or MQTT they travel encrypted (`lmcmd.py wifi`); on serial also `/maint.wifi <ssid> [<password>]` |
| `/ota.server [<url>\|default]` | local | Shows or sets the OTA server (bench) |
| `/reboot` | Signed | Restarts the node |
| `/getRT` | Open | Routing table |
| `/getIP` | Open | Gateway IP address |
| `/addSSID`, `/addPassword` | local | Gateway WiFi credentials (prefer `/maint.wifi`) |
| `/connectWiFi`, `/resetWiFiData` | Signed | Gateway WiFi |
| `/displayOn [<addr>]`, `/displayOff [<addr>]` | Open | Turns the display on for 60 s, or off |
| `/displayBlink [<addr>]`, `/displayLogo [<addr>]` | Open | Blinks the display for 5 s, or shows the logo for 1 s |
| `/displayText [<addr>] <text>` | Open | Shows one line on the last row for five minutes (the first 14 characters fit beside the version). A new line replaces the old one. It does not turn the display on |
| `/displayClear [<addr>]` | Open | Removes that line |

In the display commands `<addr>` is the destination node as exactly four hex digits (`0A3F`). Any other first word belongs to the text, so `/displayText add milk` shows `add milk` on this node. A text that starts with a four-digit hex word needs the node's own address in front. A node without a working display still sends display commands to other nodes. Text sent to another node takes up to 100 characters at SF9 (one LoRa packet), and longer text is refused.

One LoRa packet carries the command frame, so a signed line has room for about 89 characters at SF9 (LoRaMesher limits packets to 115 bytes there). A reply has room for 100 characters there, and a longer one is cut and ends in `~`.

Sensor nodes can let the ESP32 sleep while the mesh has nothing for them (`/sleep.set`, default `none`). The radio always sleeps in the SLEEP slots. `light` puts the MCU into light sleep through each run of SLEEP slots. `deep` deep-sleeps through runs long enough for a reboot (`LORA_DEEP_SLEEP_MIN_MS` plus `LORA_DEEP_SLEEP_BOOT_MS` in `src/config.h`) and light-sleeps through shorter ones. In deep-sleep mode the node keeps its mesh state in RTC memory and resumes its slots after the wake-up, without the display, the charger restart or the WiFi boot window, and the wake-up does not count as a boot. Long sleep runs need a gateway with a higher `/sleep.fraction`. A node stays awake while its display is on, in install mode, during a maintenance window and while a new image is being verified. The gateway never sleeps. A node also stays awake while the power chip signals an event that has not been read yet, and logs a warning if that lasts more than 5 s.

The power button ends a light sleep at once (`PMU_KEY_WAKES_LIGHT_SLEEP` in `src/config.h`, on by default). LoRaMesher then corrects its clock with the time the node really slept, so the node keeps its slots. The display stays off after a deep-sleep wake-up, and the button neither turns it on nor ends a deep sleep. The firmware uses LoRaMesher `release/2.0.0` at commit `368c359`, which holds the early-wake fix of light sleep (pull request 145). The commit is pinned in `platformio.ini`.

Sensor nodes switch WiFi on only in a maintenance window: for up to 60 s after every boot (not while a newly installed image is being verified) they join the node's WiFi network, check the OTA server for a newer signed image and install it, then switch WiFi off again. They use the same credentials as a gateway: those stored in NVS, otherwise `WIFI_SSID`/`WIFI_PASSWORD` from `config_local.h`. A sensor never becomes a mesh gateway through this WiFi.

## Energy measurements

[docs/energy_ppk2.md](docs/energy_ppk2.md) describes the energy bench with a Nordic PPK2: the wiring on the T-Beam header (5V pin, or the battery holder for battery life), the tests, `scripts/energy/ppk2_bench.py` for recording (current plus serial logs on one clock) and `scripts/energy/ppk2_report.py` for the analysis (charge, power, bursts, superframe profile, battery life).

## MQTT messages

Messages travel as JSON on two topics:

- `from-server/<gateway id>` (`MQTT_TOPIC_SUB`): messages from the server to the mesh. The `addrDst` field selects the destination node.
- `to-server/<gateway id>` (`MQTT_TOPIC_OUT`): messages from the mesh to the server.
- `cmd/<gateway hex>`, `cmd-resp/<node hex>` and `cmd-ack/<node hex>`: text commands, their replies and their progress (see [Commands](#commands)).

Every message carries a common header followed by the payload of the application that produced it. The application ports are listed in `src/message/dataMessage.h`.

```json
{
  "data": {
    "appPortDst": 8,
    "appPortSrc": 16,
    "addrDst": 0,
    "addrSrc": 10832,
    "messageId": 2,
    "messageSize": 30
  }
}
```

The fields after the header depend on the application. Monitor reports (`appPortSrc` 16) only travel to the server; the gateway drops any it receives from the server.

A monitor report carries the node's uptime, queues, report interval, battery voltage and power flags, and for each direct neighbour its RSSI and SNR (report format 2, `"v": 2` in the JSON). `interval_s` is the time until the next report: 300 s normally and two superframes in install mode, when `install` is 1. The gateway adds `sf_ms`, the duration of its superframe in ms when it builds the JSON. A node with an environment sensor adds `temp_c`, `hpa` and, with a BME280, `rh`. A gateway decodes both formats, but a gateway with older firmware drops format 2 reports, so flash the gateway before the other nodes.

## More information on the design and evaluation of LoRaChat
Please see our open access paper ["Middleware for Distributed Applications in a LoRa Mesh Network"]([https://ieeexplore.ieee.org/document/9930341](https://dl.acm.org/doi/10.1145/3747295)) for a detailed description. If you use the LoRaChat, in academic work, please cite the following:
```
@article{10.1145/3747295,
author = {Miquel Sol\'{e}, Joan and Pueyo Centelles, Roger and Freitag, Felix and Meseguer, Roc and Baig, Roger},
title = {Middleware for Distributed Applications in a LoRa Mesh Network},
year = {2025},
issue_date = {July 2025},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
volume = {24},
number = {4},
issn = {1539-9087},
url = {https://doi.org/10.1145/3747295},
doi = {10.1145/3747295},
abstract = {Recently, LoRa mesh networks have gained increasing interest as a communication layer for sending data between IoT nodes. However, the network service of the firmware on the microcontroller-based nodes is typically limited to sending and receiving LoRa packets through the LoRa radio. Therefore, the packet processing by the node has to be done using an application-specific implementation. In this article, we present the design and implementation of a middleware that facilitates the development and operation of multiple distributed applications on LoRa mesh network nodes. The components we propose leverage the routing capacity of a LoRa mesh network enabled by the LoRaMesher library and provide a service for applications to send and receive messages from each other. Running several applications concurrently is also supported. We experiment with the middleware implemented in the node firmware with distributed applications that span from the LoRa mesh network to the Internet over MQTT. Our results show the support of bidirectional application-level communication, which can be used to build cross-network distributed applications that integrate services on LoRa mesh network nodes.},
journal = {ACM Trans. Embed. Comput. Syst.},
month = jul,
articleno = {60},
numpages = {26},
keywords = {LoRa, mesh network, IoT, distributed application}
}
```

# Acknowledgements

LoRaChat stands on the work of many open-source projects. Thank you to their authors and maintainers.

### Firmware

| Project | Author | License | Used for |
|---|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Espressif | Apache-2.0 | Framework, WiFi, OTA, NVS, HTTP |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | Espressif | LGPL-2.1+ | Arduino core on top of ESP-IDF |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Trusted Firmware | Apache-2.0 | SHA-256, HMAC, ECDSA (shipped with ESP-IDF) |
| [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) | Joan Miquel Solé | MIT | LoRa mesh protocol |
| [RadioLib](https://github.com/jgromes/RadioLib) | Jan Gromes | MIT | LoRa radio driver |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | Lewis He | MIT | AXP192 / AXP2101 power management |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library), [SSD1306](https://github.com/adafruit/Adafruit_SSD1306), [BusIO](https://github.com/adafruit/Adafruit_BusIO) | Adafruit | BSD / MIT | OLED display |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Benoît Blanchon | MIT | JSON messages |

### Tests and tools

| Project | Author | License | Used for |
|---|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | Google | BSD-3-Clause | Native unit tests |
| [pytest](https://github.com/pytest-dev/pytest) | pytest-dev | MIT | Host tool tests |
| [clang-tidy](https://clang.llvm.org/extra/clang-tidy/), [clang-format](https://clang.llvm.org/docs/ClangFormat.html) | LLVM Project | Apache-2.0 WITH LLVM-exception | Static analysis and format check (run as tools) |
| [Cppcheck](https://cppcheck.sourceforge.io) | Cppcheck team | GPL-3.0 | Static analysis (run as a tool) |
| [pySerial](https://github.com/pyserial/pyserial) | Chris Liechti | BSD-3-Clause | Serial access from host scripts |
| [esptool](https://github.com/espressif/esptool) | Espressif | GPL-2.0+ | Flashing and reading flash (run as a tool, not distributed with the firmware) |
| [ppk2-api](https://github.com/IRNAS/ppk2-api-python) | IRNAS | GPL-2.0 | PPK2 control in `ppk2_bench.py` (host tool, not distributed with the firmware) |
| [NumPy](https://numpy.org), [pandas](https://pandas.pydata.org), [Matplotlib](https://matplotlib.org) | their communities | BSD-3-Clause, BSD-3-Clause, Matplotlib (PSF-based) | Trace analysis in `ppk2_report.py` |

The LGPL components are linked into the firmware. LoRaChat is MIT licensed and its full source is published, so you can rebuild and relink the firmware with modified versions of those libraries.

# Disclaimer

This project is still in development. It is not ready for production. We are still working on it.

Additionally, if you use the TTGO T-Beam be aware that the WiFi antenna should be mounted correctly for the WiFi connect all the times. (It just happened to me to spend a lot of time trying to figure out why the WiFi was not working.)

# ICDCS 2022 Demonstration

![ICDCS demonstration](images/ICDCS2022.png)
