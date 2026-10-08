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

The power management chip is detected at boot. The firmware powers the LoRa radio, the ESP32 and the rail of the OLED and the 3.3 V header pins, and switches the GPS off (it is not used). Batteries are charged to 4.2 V. The user LED (GPIO4) is kept off; the charge LED is driven by the charger: on while charging, off when full or on battery, blinking on a charge fault (for example no battery inserted).

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

## Versions, releases and updates

- **Version:** `version.txt` (`MAJOR.MINOR.PATCH`). Every build writes it into the image with the git commit, e.g. `0.1.0+gabc1234`, and the node prints it at boot: `BOOT part=app0 state=VALID ver=0.1.0+gabc1234 ...`.
- **Release:** after a build, `python3 scripts/ota_tools/release.py --env tbeam --sign-key <key.pem> --key-id <id>` checks the image and writes `release/tbeam/<version>/` with the images, `info.json`, the signed `manifest.bin` and `bundle.bin`.
- **Keys:** images are signed with ECDSA P-256. Key id 1 is the test key in `test/vectors/test_key.pem` for bench and test images; production builds (`OTA_PRODUCTION`) refuse it. Production keys are created with `sign_manifest.py keygen` and kept offline; their public keys go into `src/ota/otaKeys.cpp`.
- **Rollback:** the flash holds two app slots. A newly installed image boots in `PENDING_VERIFY`; the boot guard (`src/ota/otaBootGuard.cpp`) runs a self-test (radio, power chip, heap, signature check, mesh contact) under a watchdog and either keeps the image or makes the bootloader return to the previous one. Images that fail are blacklisted.
- **Over-the-air download:** the signed manifest, the update policy and the boot guard are implemented; the WiFi download from a server or phone hotspot and the upload through the node's own access point are being built (see `todo.md`).

Commands for keys, signing, inspecting a manifest (`sign_manifest.py show`), test images and bench flashing are in [scripts/ota_tools/README.md](scripts/ota_tools/README.md).

## Configuration

Site settings are kept out of git. Copy `src/config_local.example.h` to `src/config_local.h` and set:

- `WIFI_SSID`, `WIFI_PASSWORD`: default WiFi network of the gateway. Credentials stored in NVS with the `/addSSID` and `/addPassword` commands take priority.
- `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USERNAME`, `MQTT_PASSWORD`: MQTT broker. MQTT is disabled when `MQTT_SERVER` is empty.
- `LORA_MANAGER_ID`: address of the node that acts as LoRaMesher network manager.

Product settings (features, LoRa radio parameters, monitor period) are in `src/config.h`, board pins in `src/boards/tbeam.h`. ESP-IDF options are in `sdkconfig.<environment>`.

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
