# LoRaChat Firmware

LoRaChat is ESP32 firmware for LoRa mesh networks of sensor nodes. It runs on TTGO T-Beam boards and uses [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) 2.0, a TDMA LoRa mesh protocol, to carry application messages between nodes. A gateway node bridges the mesh to an MQTT broker over WiFi. Firmware updates are installed over the air with automatic rollback.

You can join LoRaMesher's developers and users on **[Discord](https://discord.gg/SSaZhsChjQ)**, if you like.

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

The power management chip is detected at boot. The firmware powers the LoRa radio, the ESP32 and the rail of the OLED and the 3.3 V header pins, and switches the GPS off (it is not used). Batteries are charged to 4.2 V. The user LED (GPIO4) is kept off. With a battery fitted, the charge LED is driven by the charger: on while charging, off when full or on battery, blinking on a charge fault; without a battery it stays off (the T-Beam v1.2 reports battery insertion and removal, and the LED follows). The charger is restarted at every boot, which clears a latched charge fault as unplugging USB does. `/power` prints supply, battery and charger state (an AXP192 without a battery reports a phantom cell at about 3.3 V that does not charge).

The OLED shows the node address and version for 60 s after boot (`DISPLAY_AWAKE_MS` in `src/config.h`) and then sleeps. A short press of the power button (PWR) wakes it for another 60 s; holding the button for 4 s switches the board off. The `/displayOn` and `/displayOff` commands wake it and put it to sleep.

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
```

`scripts/pio.sh` wraps `pio`. Under WSL it keeps the PlatformIO workspace on the Linux filesystem, which makes builds several times faster than on a Windows drive.

The native tests compile the mbedTLS of the ESP-IDF package for the host, so build `tbeam` once before the first `scripts/pio.sh test -e native`. The host tools need Python 3 with `pip install -r scripts/ota_tools/requirements.txt`.

CI (`.github/workflows/firmware.yml`) runs on every push: the rollback guards, both builds with the image checks and the 90 % size gate of `release.py`, the license gate, the native tests and the host tool tests, and keeps the unsigned images as a build artifact.

## Versions, releases and updates

- **Version:** `version.txt` (`MAJOR.MINOR.PATCH`). Every build writes it into the image with the git commit, e.g. `0.1.0+gabc1234`, and the node prints it at boot: `BOOT part=app0 state=VALID ver=0.1.0+gabc1234 ...`.
- **Release:** after a build, `python3 scripts/ota_tools/release.py --env tbeam --sign-key <key.pem> --key-id <id>` checks the image and writes `release/tbeam/<version>/` with the images, `info.json`, the signed `manifest.bin` and `bundle.bin`.
- **Keys:** images are signed with ECDSA P-256. Key id 1 is the test key in `test/vectors/test_key.pem` for bench and test images; production builds (`OTA_PRODUCTION`) refuse it. Production keys are created with `sign_manifest.py keygen` and kept offline; their public keys go into `src/ota/otaKeys.cpp`.
- **Rollback:** the flash holds two app slots. A newly installed image boots in `PENDING_VERIFY`; the boot guard (`src/ota/otaBootGuard.cpp`) runs a self-test (radio, power chip, heap, signature check, mesh contact) under a watchdog and either keeps the image or makes the bootloader return to the previous one. Images that fail are blacklisted.
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
- **MQTT** (gateway): publish `<node> <request id> <command>` on `cmd/<gateway>`; the reply is published on `cmd-resp/<node>` as `<request id> <reply>`. A command for another node is sent on over LoRa.

Every command has a permission: **Open** commands run from any channel, **Signed** commands need a signature when they come over LoRa or MQTT, and **local** commands only run on the serial console. A signature is ` #<counter>.<tag>` at the end of the line, made with the deployment key by `scripts/ota_tools/lmcmd.py sign` (see [scripts/ota_tools/README.md](scripts/ota_tools/README.md)). A node runs each counter once: an older counter gets `ERR replay`, the same counter again gets the stored reply. Remote errors are short: `ERR unknown`, `ERR perm`, `ERR auth`, `ERR replay`.

| Command | Permission | Effect |
|---|---|---|
| `/help` | Open | Lists the commands the channel may run |
| `/version` | Open | Version, slot, image state, bootloader hash, board and address |
| `/ota.status` | Open | Last update attempt and its outcome |
| `/role` | Open | The role and whether it is stored or the default |
| `/power` | Open | Supply, battery and charger state (with the AXP2101 registers) |
| `/role.set gateway`, `/role.set sensor` | Signed | Stores the role and restarts the node. Refused while a newly installed image is still being verified |
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
| `/displayOn`, `/displayOff`, `/displayBlink`, `/displayClear`, `/displayLogo`, `/displayText` | Open | Display |

One LoRa packet carries the command frame, so a signed line has room for about 89 characters at SF9 (LoRaMesher limits packets to 115 bytes there); longer replies end in `~`.

Sensor nodes switch WiFi on only in a maintenance window: for up to 60 s after every boot (not while a newly installed image is being verified) they join the node's WiFi network, check the OTA server for a newer signed image and install it, then switch WiFi off again. They use the same credentials as a gateway: those stored in NVS, otherwise `WIFI_SSID`/`WIFI_PASSWORD` from `config_local.h`. A sensor never becomes a mesh gateway through this WiFi.

## MQTT messages

Messages travel as JSON on two topics:

- `from-server/<gateway id>` (`MQTT_TOPIC_SUB`): messages from the server to the mesh. The `addrDst` field selects the destination node.
- `to-server/<gateway id>` (`MQTT_TOPIC_OUT`): messages from the mesh to the server.

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

The fields after the header depend on the application.

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
| [pySerial](https://github.com/pyserial/pyserial) | Chris Liechti | BSD-3-Clause | Serial access from host scripts |
| [esptool](https://github.com/espressif/esptool) | Espressif | GPL-2.0+ | Flashing and reading flash (run as a tool, not distributed with the firmware) |

The LGPL components are linked into the firmware. LoRaChat is MIT licensed and its full source is published, so you can rebuild and relink the firmware with modified versions of those libraries.

# Disclaimer

This project is still in development. It is not ready for production. We are still working on it.

Additionally, if you use the TTGO T-Beam be aware that the WiFi antenna should be mounted correctly for the WiFi connect all the times. (It just happened to me to spend a lot of time trying to figure out why the WiFi was not working.)

# ICDCS 2022 Demonstration

![ICDCS demonstration](images/ICDCS2022.png)
