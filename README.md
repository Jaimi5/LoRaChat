# LoRaChat Firmware

LoRaChat Firmware is a versatile communication system that utilizes LoRa technology and ESP32 LoRa boards to facilitate long-distance communication. With the convenience of a Bluetooth Serial Terminal, you can easily connect to your devices through your phone. Our implementation includes MQTT support, allowing for communication between the device and the LoRaChat Firmware system.

We are using the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher), which implements a LoRa Mesh communication protocol to establish routing tables and contact lists. This protocol allows our system to dynamically adjust to changes in the network topology, ensuring reliable communication over long distances. Additionally, our system supports large messages thanks to the LargePayloads feature, enabling you to transmit data of any size with ease.

## Installation

We are using platformio to compile and upload the code to the device. You can install it from [here](https://platformio.org/install/ide?install=vscode).

We are using EMQX as our MQTT broker. You can install it from [here](https://www.emqx.io/downloads).

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


# Testing the library

There is a Testing application that can be used to test the library. It is located in the `Testing` directory.
After executing the Testing applications, there is too an UI application that can be used to visualize the results. It is located in the `UI` directory.

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

- Add the devices COM port

Call firstly the next command to get the com ports of your devices:

```bash
./Testing/main.py directoryName -p
```

- Execute requirements.txt

Run the following command to install the requirements:

```bash
pip install -r ./Testing/requirements.txt
```

After that, go to ./Testing/updatePlatformio.py and add the com ports of the devices like this:

```python
envPort = {
    "COM12": "ttgo-lora32-v1",
    "COM14": "ttgo-lora32-v1",
    "COM32": "ttgo-t-beam"
}
```

Adding their respective platformio environment.

- Initiate the script

Run the following command to initiate the script:

```bash
./Testing/main.py directoryName
```

The `directoryName` is the name of the directory where the results will be saved. If the directory does not exist it will be created.

After that it will ask you some questions, the most important part is to add the packet size you want to test. The packet size is the size of the payload of the message. The header of the message is not included in the packet size.

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

# Disclaimer

This project is still in development. It is not ready for production. We are still working on it.

Additionally, if you use the TTGO T-Beam be aware that the WiFi antenna should be mounted correctly for the WiFi connect all the times. (It just happened to me to spend a lot of time trying to figure out why the WiFi was not working.)

# ICDCS 2022 Demonstration

![ICDCS demonstration](images/ICDCS2022.png)
