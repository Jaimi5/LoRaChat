# LoRaChat Firmware

LoRaChat Firmware is a versatile communication system that utilizes LoRa technology and ESP32 LoRa boards to facilitate long-distance communication. With the convenience of a Bluetooth Serial Terminal, you can easily connect to your devices through your phone. Our implementation includes MQTT support, allowing for communication between a device in Internet and the LoRaChat Firmware system.

We are using [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher), which implements a LoRa Mesh communication protocol to establish routing tables and contact lists. This protocol allows our system to dynamically adjust to changes in the network topology, ensuring reliable communication over long distances with multi-hop. Additionally, our system supports large messages thanks to the LargePayloads feature, enabling you to transmit data of any size with ease.


You can join LoRaMesher's developers and users on **[Discord](https://discord.gg/SSaZhsChjQ)**, if you like. 

## Organizations & Contributors Using This Project

- [Hacking Ecology (Nayad)](https://hackingecology.com/) - Hacking Ecology transforms water monitoring with accessible and open-source systems.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/enclosure%20logo.png" height="60">](https://hackingecology.com/)

- [Universitat Politècnica de Catalunya](https://www.upc.edu/) and [CNDS research group](https://www.ac.upc.edu/en/research/research-groups/cnds) - The group investigates AI/ML in the IoT, Community Networks, and LoRa Mesh Networks.

[<img src="https://github.com/Jaimi5/Jaimi5.github.io/blob/master/logos/upc-positiu-p3005-interior-blanc.png" height="60">](https://www.upc.edu/)


## Demonstrator

A live demonstration of an operation LoRaChat application is [here](https://tomir.ac.upc.edu/loraupc/index.php).

## Installation

We use platformio to compile and upload the code to the device. You can install it from [here](https://platformio.org/install/ide?install=vscode).

We use EMQX as our MQTT broker. You can install it from [here](https://www.emqx.io/downloads).

To test the MQTT communication you can use the [MQTTX](https://mqttx.app/) application.

## Configuration

### Device

Before updating the code into a device you need to go to the platformio and put the specific device you want to add. After that you need to go to `src/config.h` and choose the device you want to use.

### LoRa

The LoRa configuration is done through the `loramesh/LoRaMeshService.cpp` file. When initializing you can change the default parameters, including the module of the device. See the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) documentation for more information.

### WiFi

The WiFi configuration can be done with two ways. First of all, changing the default value of the `WIFI_SSID` and `WIFI_PASSWORD` variables in the `config.h` file. The second way is to use the `wifi` command in the Bluetooth Serial Terminal. When initializing the device it will show you the commands to introduce the WiFi credentials.

### MQTT

The MQTT you can change the `MQTT_SERVER` and `MQTT_PORT` variables in the `config.h` file. You can change the `MQTT_TOPIC_SUB` and `MQTT_TOPIC_OUT` variables to change the topic where the device will receive the messages and will publish the messages respectively.
To use MQTT you need to be connected to the WiFi network. There are other parameters described in the `config.h` file.

### Example on how to send a message to the device using MQTT:

- The topic is `from-server/1234`: `from-server` is the `MQTT_TOPIC_SUB` variable and `1234` is the gateway device ID that would receive the message. You need to specify the destination of the device using the `addrDst` field in the JSON message.

- You can look at the `message/dataMessage.h` file to see the different applications that are available. We are sending the message to the LED Application. Which in the appPort enum is the `LedApp`, that is the value `13`.

- Each application has a different payload or message. In this case, we are sending a message to the LED application. The payload is an object with the following field:

  - `ledCommand`: The command to execute. In this case, we are sending the `LedCommand::On` command, which is the value `1`.

- The message is sent in JSON format. The final JSON object is:

```json
{
  "data": {
    /** The message header. */
    "appPortDst": 13, // The app port of the application that is receiving the message. In this case, the LED App.
    "appPortSrc": 13, // The app port of the application that is sending the message. In this case, the LED App.
    "addrDst": 1234, // The destination device ID
    /** The message payload. Specific for each App */
    "ledCommand": 1
  }
}


```

Since we want to implement each app in the server too, the command will be generated in the server service. That's why we are using the `appPortSrc` field to specify the application.

### Example on how we are going to receive the message in the server:

- The topic is `to-server/1234`: `to-server` is the `MQTT_TOPIC_OUT` variable and `1234` is the source device ID.

- The message will depend on which applications has generated the message. In this case, we are receiving a message from the Temperature application. The payload is an object with the following field:

  - `temperature`: The temperature value. 

- You can see all the message types starting in the `sensor/temperatureMessage.h` file and going through the BaseClasses of the Temperature message.

- The message is sent in JSON format. The final JSON object is:

```json
{
  "data": {
    /** The message header. */
    "appPortDst": 8, // The MQTT application port
    "appPortSrc": 10, // The Temperature Sensor App
    "addrDst": 1234, // The destination device ID, it will be the gateway responsible of sending the message to the server and the closest and best gateway to the node.
    "messageId": 2, // The message ID, specified by each application. In this case, the Temperature Sensor App will increase the message ID by 1 each time it sends a message.
    "addrSrc": 10832, // The source device ID
    "messageSize": 6, // The message size in bytes (not including the header)

    /** The message payload. Specific for each App */
    "temperature": -127 // The temperature value
  }
}
```

## Platformio.ini configuration

With this example we are using a Heltec WIFI LoRa 32 (V3) board. You can use any ESP32 board with LoRa support. You can change the board in the `platformio.ini` file.

Be aware that when changing the board, if it is not from the default ones, you will need to change the module in the `loramesh/LoRaMeshService.cpp` file and other configuration pins in the `src/config.h` file.

As we are using the Heltec WIFI LoRa 32 (V3) we needed to use a custom board configuration. [Here is why](http://community.heltec.cn/t/heltec-board-migration-from-v2-to-v3/12667).

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


# Testing the library

There is a Testing application that can be used to test the library. It is located in the `Testing` directory.
After executing the Testing applications, there is too an UI application that can be used to visualize the results. It is located in the `UI` directory.

## Quick Start

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

## How to use the Testing application
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

### Configuration Process

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

### New Features

#### 🔄 Automatic Retry Mechanism
- **Smart Retries**: Failed simulations automatically retry up to **5 times**
- **Retry Delay**: 15-second wait between retry attempts
- **Organized Results**: Each retry creates a separate directory (`experiment_retry1`, `experiment_retry2`, etc.)
- **Detailed Logging**: Error messages logged for each failure
- **Graceful Handling**: If all retries fail, the system moves to the next simulation

#### ⏱️ Comprehensive Timeout System
The testing framework includes multiple timeout layers to detect issues quickly:

- **Global Simulation Timeout**: Configurable (default: 45 minutes) - Overall test duration limit
- **Device Start Timeout**: 10 minutes - Ensures all devices start the simulation
- **Device End Simulation Timeout**: 15 minutes - Detects stuck simulations
- **Monitor Startup Timeout**: 5 minutes per device - Detects monitoring issues
- **Build/Upload Timeout**: 10 minutes - Prevents hanging during build

All timeouts can be configured in `simConfiguration.json` via the `SimulationTimeoutMinutes` field.

#### 📝 Configurable Device Mapping
- **No More Code Editing**: Device-to-environment mapping stored in configuration files
- **Portable Configurations**: Move test configs between different machines
- **Per-Test Customization**: Each simulation can use different device sets
- **Backward Compatible**: Old configurations still work with global fallback

> **Migration Note**: If you previously edited `updatePlatformio.py` to add COM ports manually, you can continue using that method (it still works as a fallback). However, the new interactive device mapping is recommended for easier configuration and portability.

#### Example simConfiguration.json

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

## Results

The results will be saved in the `directoryName` directory and the name of each experiment.

## Folder structure

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

# Acknowledgements

LoRaChat stands on the work of many open-source projects. Thank you to their authors and maintainers.

### Firmware

| Project | Author | License | Used for |
|---|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Espressif | Apache-2.0 | Framework, WiFi, OTA, NVS, HTTP |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | Espressif | LGPL-2.1+ | Arduino core on top of ESP-IDF |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Trusted Firmware | Apache-2.0 | SHA-256, HMAC, ECDSA (shipped with ESP-IDF) |
| [esp_delta_ota](https://components.espressif.com/components/espressif/esp_delta_ota) | Espressif | Apache-2.0 | Delta OTA patches |
| [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) | Joan Miquel Solé | MIT | LoRa mesh protocol |
| [RadioLib](https://github.com/jgromes/RadioLib) | Jan Gromes | MIT | LoRa radio driver |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | Lewis He | MIT | AXP192 / AXP2101 power management |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library), [SSD1306](https://github.com/adafruit/Adafruit_SSD1306), [BusIO](https://github.com/adafruit/Adafruit_BusIO) | Adafruit | BSD / MIT | OLED display |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Benoît Blanchon | MIT | JSON messages |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | Mikal Hart | LGPL-2.1+ | GPS parsing |
| [SparkFun u-blox Arduino Library](https://github.com/sparkfun/SparkFun_Ublox_Arduino_Library) | SparkFun Electronics | MIT | GPS configuration |
| [EspSoftwareSerial](https://github.com/plerup/espsoftwareserial) | Dirk Kaar, Peter Lerup | LGPL-2.1+ | Software serial for sensors |
| [ghostl](https://github.com/dok-net/ghostl) | Dirk Kaar | LGPL-2.1+ | Used by EspSoftwareSerial |
| [OneWire](https://github.com/PaulStoffregen/OneWire) | Jim Studt, Paul Stoffregen and contributors | MIT-style | Soil sensor bus |

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
