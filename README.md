# LoRaChat

LoRaChat is an open-source C++ middleware for ESP32 LoRa boards. It runs on top of the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) mesh routing library and adds the application layer that a mesh routing library does not provide:

- **Application ports (`appPort`)** let several distributed applications run concurrently on the same node. Each incoming message is delivered only to the application it is addressed to.
- **Message ports (`messagePort`)** identify the network a message is sent or received over: the LoRa mesh, an MQTT broker (via Wi-Fi), Bluetooth, or another application on the same node.
- **A central `MessageManager`** dispatches every message to the right application and network, so new applications can be added without touching the network layer or the applications already running.

A gateway node with Wi-Fi bridges the mesh to an MQTT broker in both directions, so Internet hosts can send commands to applications on any mesh node and receive data from them. A smartphone paired over Bluetooth can issue text commands to a node.

The repository includes example applications (LED actuation, sensor reporting, GPS, OLED display, network monitoring), a Python test harness for experiments with physical nodes, and testbed scripts.

You can join LoRaMesher's and LoRaChat's developers and users on **[Discord](https://discord.gg/KCY78MbpUa)**.

## Contents

- [Organizations and contributors using this project](#organizations-and-contributors-using-this-project)
- [Demonstrator](#demonstrator)
- [Architecture](#architecture)
- [Supported hardware and LoRaMesher versions](#supported-hardware-and-loramesher-versions)
- [Installation and build](#installation-and-build)
- [Configuration](#configuration)
- [Minimal example: from the Internet to a mesh node and back](#minimal-example-from-the-internet-to-a-mesh-node-and-back)
- [Adding a new application](#adding-a-new-application)
- [Running experiments with the test harness](#running-experiments-with-the-test-harness)
- [Known limitations](#known-limitations)
- [Citing LoRaChat](#citing-lorachat)
- [License](#license)

## Organizations and contributors using this project

- [Hacking Ecology (Nayad)](https://hackingecology.com/) - Hacking Ecology transforms water monitoring with accessible and open-source systems. LoRaChat has a dedicated build environment for their Nayad board.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/enclosure%20logo.png" height="60">](https://hackingecology.com/)

- [Universitat Politècnica de Catalunya](https://www.upc.edu/) and [CNDS research group](https://www.ac.upc.edu/en/research/research-groups/cnds) - The group investigates AI/ML in the IoT, Community Networks, and LoRa Mesh Networks.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/upc-positiu-p3005-interior-blanc.png" height="60">](https://www.upc.edu/)

If you use LoRaChat in a project, let us know through an issue or on Discord and we will add you to this list.

## Demonstrator

A live view of a permanent 12-node LoRa mesh testbed running LoRaChat's monitoring application is available [here](https://tomir.ac.upc.edu/loraupc/index.php). It shows the network topology reconstructed from the neighbor reports that each node publishes over MQTT.

## Architecture

```
  Applications      LED   Sensor   GPS   Display   Monitor   (your app)
                     │       │      │       │         │          │
                     └───────┴──────┴───┬───┴─────────┴──────────┘
                                        │  DataMessage (appPortSrc/appPortDst, addrSrc/addrDst)
                                ┌───────┴────────┐
                                │ MessageManager │   dispatch by appPort and messagePort
                                └───────┬────────┘
              ┌──────────────────┬──────┴───────────┬───────────────────┐
         LoRaMeshPort         MqttPort         BluetoothPort       InternalPort
              │                  │                  │            (same node)
      LoRaMeshService       MqttService      BluetoothService
              │             (over Wi-Fi)    (command console)
          LoRaMesher
   (0.0.11 or TDMA-based)
```

All components exchange a common `DataMessage` (defined in `src/message/dataMessage.h`). Its header carries the source and destination application ports (`appPortSrc`, `appPortDst`), the source and destination node addresses (`addrSrc`, `addrDst`), a `messageId`, and the payload size (`messageSize`). Each application extends this header with its own payload.

Source code layout (`src/`):

| Folder | Content |
|---|---|
| `message` | `DataMessage`, `MessageService` base class, `MessageManager` |
| `loramesh` | `LoRaMeshService`, the wrapper around LoRaMesher (both versions) |
| `mqtt`, `wifi`, `bluetooth` | Cross-network services |
| `commands` | `CommandService` base class for per-application text commands |
| `led`, `sensor`, `gps`, `display`, `monitor` | Example applications |
| `simulator` | On-device traffic generator used by the test harness |
| `battery`, `ota`, `time`, `helpers`, `devices` | Support modules |
| `config.h` | Feature switches, Wi-Fi/MQTT settings, radio parameters |
| `main.cpp` | Initialization and registration of all services |

Outside `src/`, `Testing/` contains the Python test harness and `scripts/testbed/` contains the scripts used to deploy and run experiments on our testbed (see [scripts/testbed/README.md](scripts/testbed/README.md)).

## Supported hardware and LoRaMesher versions

LoRaChat builds against two versions of LoRaMesher. The version is selected by the PlatformIO environment, without code changes.

- **LoRaMesher 0.0.11** (environments without suffix): the original CSMA-based distance-vector version, described in the [LoRaMesher SoftwareX paper](https://doi.org/10.1016/j.softx.2026.102570). It supports reliable transfer of large payloads (`sendReliablePacket`). Choose this version if your application sends payloads larger than one LoRa packet.
- **TDMA-based LoRaMesher** (environments with suffix `-v2`): the complete rewrite first released as [LoRaMesher v1.0.0](https://github.com/LoRaMesher/LoRaMesher/tree/v1.0.0), in which an elected network manager schedules transmissions in a superframe. For reproducible builds, `platformio.ini` pins commit [`134ae25`](https://github.com/LoRaMesher/LoRaMesher/commit/134ae25ff788c40131e3ef744126ff21f6fa038f) of the LoRaMesher `new_loramesher` branch, which contains APIs that LoRaChat needs and that are not in the `v1.0.0` tag. The build flag `USE_LORAMESHER_V2` enables this version. Reliable large-payload transfer is not yet available with it.

Applications are the same for both versions. Only `LoRaMeshService` and the monitor and simulator modules contain version-specific code.

| Board | LoRaMesher 0.0.11 | TDMA-based LoRaMesher |
|---|---|---|
| TTGO T-Beam v1.0 | `ttgo-t-beam` | `ttgo-t-beam-v2` |
| TTGO T-Beam v1.2 | `ttgo-t-beam-v1-2` | `ttgo-t-beam-v1-2-v2` |
| TTGO LoRa32 v1 | `ttgo-lora32-v1` | `ttgo-lora32-v1-v2` |
| Nayad v1r2 (Hacking Ecology) | `esp-wrover-kitNAYAD_V1R2` | not available |
| Makerfabs SenseLoRa (soil moisture) | `MAKERFABS_SENSELORA_MOISTURE` | `MAKERFABS_SENSELORA_MOISTURE-v2` |

Other ESP32 boards with a LoRa radio supported by RadioLib can be added by creating a new environment in `platformio.ini`, a board define, and the corresponding pin settings in `src/config.h` and LoRa module settings in `src/loramesh/loraMeshService.cpp`.

## Installation and build

Requirements:

- [PlatformIO](https://platformio.org/install) (CLI or the VS Code extension). PlatformIO downloads the ESP32 toolchain, the Arduino/ESP-IDF frameworks, LoRaMesher, and the other libraries automatically on the first build.
- A supported board (see the table above) and a USB cable.
- For gateway operation, a Wi-Fi network and an MQTT broker (for example [EMQX](https://www.emqx.io/downloads) or Mosquitto).

Build, flash, and monitor a node:

```bash
git clone https://github.com/Jaimi5/LoRaChat.git
cd LoRaChat
git checkout v1.0.1                                  # optional: the released version

pio run -e ttgo-t-beam                               # build
pio run -e ttgo-t-beam -t upload                     # build and flash the connected board
pio device monitor -e ttgo-t-beam                    # serial monitor (115200 baud)
```

Replace `ttgo-t-beam` with the environment for your board and LoRaMesher version. The first build takes several minutes because PlatformIO installs the toolchain and frameworks.

## Configuration

All settings are in `src/config.h`.

### Board and features

The board is selected by the PlatformIO environment, which sets a board define (for example `T_BEAM_V10`). For each board, `config.h` contains a block of feature switches such as `LED_ENABLED`, `DISPLAY_ENABLED`, `GPS_ENABLED`, `WIFI_ENABLED`, `MQTT_ENABLED`, `MQTT_MON_ENABLED` (monitor application), `BLUETOOTH_ENABLED`, and `SIMULATION_ENABLED`. Comment or uncomment them to choose which services are initialized on that board.

### Wi-Fi and gateway role

Set `WIFI_SSID` and `WIFI_PASSWORD`. A node that connects to Wi-Fi automatically announces itself as a gateway in the mesh. Nodes that cannot connect act as regular mesh nodes and send MQTT-bound messages to the closest gateway. The Wi-Fi credentials can also be changed at run time with the `wifi` command over the Bluetooth serial console.

### MQTT

Set `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USERNAME`, and `MQTT_PASSWORD`. A gateway subscribes to `MQTT_TOPIC_SUB` + its own address in decimal (by default `from-server/<address>`) and publishes messages from the mesh on `MQTT_TOPIC_OUT` + the source node address (by default `to-server/<addrSrc>`).

### LoRa radio and sending mode

The radio parameters (`LORA_FREQUENCY`, `LORA_SPREADING_FACTOR`, and others) are in `config.h`. The default frequency is 869.525 MHz in the EU 868 MHz band. Check the regulations of your region before deploying. With LoRaMesher 0.0.11, set `SEND_RELIABLE` to 1 to send every message with `sendReliablePacket()` (acknowledged, with fragmentation for large payloads). The default, 0, uses unacknowledged `createPacketAndSend()`.

## Minimal example: from the Internet to a mesh node and back

You need at least two boards flashed with LoRaChat, an MQTT broker reachable from your Wi-Fi network, and an MQTT client such as [MQTTX](https://mqttx.app/) or `mosquitto_pub`/`mosquitto_sub`.

1. Start a broker, for example with Docker:

   ```bash
   docker run -d --name emqx -p 1883:1883 -p 18083:18083 emqx/emqx
   ```

2. In `src/config.h`, set the Wi-Fi credentials and `MQTT_SERVER` to the IP address of the broker host, and make sure `LED_ENABLED` is defined for the board that should switch its LED. Flash all boards. The board that connects to Wi-Fi becomes the gateway. Each node prints its address on the serial monitor.

3. **Server to node.** To switch on the LED of node `5284` through gateway `1234` (addresses in decimal), publish on the topic `from-server/1234`:

   ```json
   {
     "data": {
       "appPortDst": 13,
       "appPortSrc": 13,
       "addrDst": 5284,
       "ledCommand": 1
     }
   }
   ```

   - `appPortDst` and `appPortSrc` are the application ports (`13` is `LedApp`, see `src/message/dataMessage.h`).
   - `appPortSrc` is required. The gateway uses it to select the application whose message class parses the payload, because the payload format is defined by the sending application. The Internet host acts as a remote instance of that application.
   - `addrDst` is the destination node. If it is omitted, the address in the topic (the gateway itself) is used. If it names another node, the gateway forwards the message through the mesh.
   - `ledCommand` is the LED application payload (`1` = on, `0` = off, see `src/led/ledMessage.h`).

4. **Node to server.** Subscribe to `to-server/#`. Every node with the monitor application enabled (`MQTT_MON_ENABLED`) periodically sends its direct neighbors to the closest gateway, which publishes them on `to-server/<addrSrc>`:

   ```json
   {"RT": {"messageId": 3, "addrSrc": 5284, "addrDst": 1234, "messageSize": 26,
           "RTcount": 1, "uptime": 86400000, "TxQ": 0, "RxQ": 0, "number_of_neighbors": 2,
           "rt": [{"neighbor": 56664, "next_hop": 56664, "link_quality": 200, "hop_count": 1},
                  {"neighbor": 13352, "next_hop": 13352, "link_quality": 190, "hop_count": 1}]}}
   ```

   The values above are illustrative. The fields are defined in `src/monitor/monServiceMessage.h`. `uptime` is in milliseconds, and `link_quality` is reported only with the TDMA-based LoRaMesher (it is 0 with 0.0.11). The reporting period is `MON_SENDING_EVERY` in `config.h`.

## Adding a new application

Every application consists of three parts. The LED application (`src/led/`) is the simplest template, and the monitor application (`src/monitor/`) is a complete example that sends data over MQTT.

1. **Reserve an application port.** Add a new value to the `appPort` enum in `src/message/dataMessage.h`. The value must be the same on all nodes and on the Internet hosts that talk to the application.

2. **Define the payload** as a subclass of `DataMessageGeneric` (for example `src/myapp/myAppMessage.h`). Add your fields and implement `serialize()` and `deserialize()` for the JSON representation, calling the base class methods first. See `src/led/ledMessage.h`.

3. **Implement the application** as a singleton subclass of `MessageService`, constructed with your port and a name, for example `MyAppService() : MessageService(MyApp, "MyApp")` for a new port `MyApp`. Override:
   - `processReceivedMessage(messagePort port, DataMessage* message)` to handle messages addressed to your port. It only receives messages for your `appPort`, whether they arrived from the mesh, MQTT, Bluetooth, or another application on the node.
   - `getDataMessage(JsonObject data)` to build your message from JSON received over MQTT.
   - `getJSON(DataMessage* message)` to serialize your message when it is published over MQTT.

   To send, fill in the header (`appPortSrc`, `appPortDst`, `addrDst`, `messageSize`) and call `MessageManager::getInstance().sendMessage(port, message)` with `LoRaMeshPort` to reach another node or `MqttPort` to reach the Internet (`appPortDst` = `MQTTApp`).

4. **Optionally declare text commands** in a subclass of `CommandService` (see `src/led/ledCommandService.*`). Commands are available over the Bluetooth serial console and through `MessageManager::executeCommand()`.

5. **Wire it into the firmware.**
   - Add a feature switch, for example `#define MYAPP_ENABLED`, to the board block in `src/config.h`.
   - In `src/main.cpp`, include your header, initialize the service under `#ifdef MYAPP_ENABLED`, and register it in `initManager()` with `manager.addMessageService(&myApp);`.

No change to `LoRaMeshService`, `MessageManager`, or the existing applications is needed.

## Running experiments with the test harness

The Python test harness in `Testing/` builds and flashes the connected nodes, runs traffic experiments with the `simulator` module, and collects every message that reaches the MQTT broker. For experiments on a permanent testbed, including runs of the same batch under both LoRaMesher versions (`scripts/testbed/run_compare.sh`), see [scripts/testbed/README.md](scripts/testbed/README.md).
After executing the Testing applications, there is too an UI application that can be used to visualize the results. It is located in the `Testing/ui` directory.

### Quick Start

```bash
# 1. Install dependencies
pip install -r ./Testing/requirements.txt

# 2. Start MQTT broker (Docker)
docker run -d --name emqx -p 1883:1883 -p 8083:8083 emqx/emqx

# 3. Update WiFi credentials in src/config.h
# WIFI_SSID and WIFI_PASSWORD

# 4. Run the testing script
python ./Testing/main.py my_experiment

# 5. Follow interactive prompts:
#    - Configure simulation parameters
#    - Connect devices one-by-one (auto-detected)
#    - Select environment for each device
#    - Configure network topology (optional)

# 6. Tests run automatically with retry support!
```

### How to use the Testing application
- Initiate a MQTT broker

We are using EMQX as our MQTT broker. You can install it from [here](https://www.emqx.io/downloads).
You can use docker and the following command to initiate the broker:

```bash
docker run -d --name emqx -p 1883:1883 -p 8083:8083 -p 8883:8883 -p 8084:8084 -p 18083:18083 emqx/emqx
```

- Get the IP address your local network

Get your IP address of your local network and add it to the `config.h` file in the `MQTT_SERVER` variable.

- Add your WiFi credentials

Add your WiFi credentials to the `config.h` file in the `WIFI_SSID` and `WIFI_PASSWORD` variables.

- (Optional) Check available COM ports

If you want to see which ports are currently available, you can run:

```bash
python ./Testing/main.py directoryName -p
```

**Note**: This step is optional! The new interactive device mapping will automatically detect ports as you connect each device during configuration.

- Execute requirements.txt

Run the following command to install the requirements:

```bash
pip install -r ./Testing/requirements.txt
```

This will install the following dependencies:
- **platformio**: Device management and compilation
- **paho-mqtt**: MQTT client for device communication
- **networkx**: Graph operations for network topology
- **matplotlib**: Visualization and plotting
- **colorama**: Colored terminal output
- **numpy**: Numerical computations and matrix operations
- **pandas**: Data analysis and manipulation

- Initiate the script

Run the following command to initiate the script:

```bash
python ./Testing/main.py directoryName
```

The `directoryName` is the name of the directory where the results will be saved. If the directory does not exist it will be created.

#### Configuration Process

The script will guide you through an interactive configuration process:

1. **Basic Configuration**: You'll be asked for simulation parameters (packet count, packet delay, packet size, etc.)

2. **Interactive Device Mapping** (NEW FEATURE):
   - The system will detect devices **one by one** as you connect them
   - For each device:
     - Start with devices disconnected (or disconnect all)
     - Connect a device
     - Press Enter
     - System automatically detects the new port (e.g., "COM7")
     - Select the PlatformIO environment from the list:
       1. ttgo-t-beam
       2. ttgo-t-beam-v1-2
       3. ttgo-lora32-v1
       4. esp-wrover-kitNAYAD_V1R2
       5. MAKERFABS_SENSELORA_MOISTURE
     - Repeat for each device
     - Type 'done' when all devices are configured

   This mapping is saved in `simConfiguration.json` and can be reused across tests!

3. **Adjacency Graph** (Optional): Configure network topology if needed

The configuration is saved and can be reused or modified for future tests.

#### New Features

##### 🔄 Automatic Retry Mechanism
- **Smart Retries**: Failed simulations automatically retry up to **5 times**
- **Retry Delay**: 15-second wait between retry attempts
- **Organized Results**: Each retry creates a separate directory (`experiment_retry1`, `experiment_retry2`, etc.)
- **Detailed Logging**: Error messages logged for each failure
- **Graceful Handling**: If all retries fail, the system moves to the next simulation

##### ⏱️ Comprehensive Timeout System
The testing framework includes multiple timeout layers to detect issues quickly:

- **Global Simulation Timeout**: Configurable (default: 45 minutes) - Overall test duration limit
- **Device Start Timeout**: 10 minutes - Ensures all devices start the simulation
- **Device End Simulation Timeout**: 15 minutes - Detects stuck simulations
- **Monitor Startup Timeout**: 5 minutes per device - Detects monitoring issues
- **Build/Upload Timeout**: 10 minutes - Prevents hanging during build

All timeouts can be configured in `simConfiguration.json` via the `SimulationTimeoutMinutes` field.

##### 📝 Configurable Device Mapping
- **No More Code Editing**: Device-to-environment mapping stored in configuration files
- **Portable Configurations**: Move test configs between different machines
- **Per-Test Customization**: Each simulation can use different device sets
- **Backward Compatible**: Old configurations still work with global fallback

> **Migration Note**: If you previously edited `updatePlatformio.py` to add COM ports manually, you can continue using that method (it still works as a fallback). However, the new interactive device mapping is recommended for easier configuration and portability.

##### Example simConfiguration.json

```json
{
  "SimulationTimeoutMinutes": "45",
  "DeviceMapping": {
    "COM7": "ttgo-t-beam",
    "COM9": "ttgo-t-beam",
    "COM11": "ttgo-lora32-v1"
  },
  "Simulator": {
    "PACKET_COUNT": "200",
    "PACKET_DELAY": "120000",
    "PACKET_SIZE": "50"
  },
  "LoRaMesher": {
    "LM_BAND": "869.900F",
    "LM_POWER": "6"
  },
  "LoRaMesherAdjacencyGraph": []
}
```

### Results

The results will be saved in the `directoryName` directory and the name of each experiment.

### Folder structure

The folder structure is the following:

```bash
directoryName
├── experiment1 # The name of the experiment 1
│   ├── Monitoring # The monitoring of the experiment
│   │   ├── build.txt # The build log
│   │   ├── Device1 # The device 1 log
│   │   ├── Device2 # The device 2 log
│   ├── data.json # The data of the experiment (All the data that reaches the MQTT broker)
│   ├── messages.json # The messages only of the experiment (Messages from the Simulation application of the devices)
│   ├── simConfiguration.json # The configuration of the simulation (You can grab and modify it to repeat the experiment)
│   ├── status.json # The status of each device
│   ├── summary.json # The summary of the experiment (The summary of the experiment)
├── experiment2 # The name of the experiment 2
```

## Known limitations

- **Security.** LoRaChat, like LoRaMesher, does not encrypt or authenticate messages. Payloads are transmitted as supplied by the application. Where needed, confidentiality and authentication must be added in the application.
- **Wi-Fi and Bluetooth ports.** `WiFiPort` is reserved, but Wi-Fi is currently used only as the transport for MQTT. Bluetooth provides a text command console and displays messages addressed to `BluetoothApp`, but it does not forward arbitrary `DataMessage`s to the mesh.
- **Large payloads.** Reliable, fragmented transfer of large payloads is available only with LoRaMesher 0.0.11. With the TDMA-based LoRaMesher, every message must fit into one LoRa packet.
- **Application ports** are 8-bit values, and their assignment must be identical on all nodes and Internet hosts. Each node can run as many applications as its memory allows.
- **Research software.** LoRaChat is developed and used for research on LoRa mesh networks. It has run for long periods on our testbed, but it has not been hardened for production deployments.
- **Hardware note.** With the TTGO T-Beam, make sure the Wi-Fi antenna is mounted correctly, otherwise gateway nodes may fail to connect.

## Citing LoRaChat

If you use LoRaChat in academic work, please cite:

J. Miquel Solé, R. Pueyo Centelles, F. Freitag, R. Meseguer, R. Baig. *Middleware for Distributed Applications in a LoRa Mesh Network.* ACM Transactions on Embedded Computing Systems 24(4), Article 60, 2025. [doi:10.1145/3747295](https://doi.org/10.1145/3747295)

```
@article{10.1145/3747295,
author = {Miquel Sol\'{e}, Joan and Pueyo Centelles, Roger and Freitag, Felix and Meseguer, Roc and Baig, Roger},
title = {Middleware for Distributed Applications in a LoRa Mesh Network},
year = {2025},
publisher = {Association for Computing Machinery},
volume = {24},
number = {4},
articleno = {60},
numpages = {26},
journal = {ACM Trans. Embed. Comput. Syst.},
doi = {10.1145/3747295},
url = {https://doi.org/10.1145/3747295}
}
```

For the underlying mesh library, please also cite LoRaMesher: J. Miquel Solé, F. Freitag, R. Baig. *LoRaMesher: An open-source library for multi-hop LoRa mesh networks.* SoftwareX 34, 102570, 2026. [doi:10.1016/j.softx.2026.102570](https://doi.org/10.1016/j.softx.2026.102570)

## License

LoRaChat is released under the MIT License. See [LICENSE.txt](LICENSE.txt).

## ICDCS 2022 Demonstration

![ICDCS demonstration](images/ICDCS2022.png)
