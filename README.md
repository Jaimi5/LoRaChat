# LoRaChat

LoRaChat is a versatile communication system that utilizes LoRa technology and ESP32 LoRa boards to facilitate long-distance communication. With the convenience of a Bluetooth Serial Terminal, you can easily connect to your devices through your phone. Our implementation includes MQTT support, allowing for communication between the device and the LoRaChat system.

We are using the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher), which implements a LoRa Mesh communication protocol to establish routing tables and contact lists. This protocol allows our system to dynamically adjust to changes in the network topology, ensuring reliable communication over long distances. Additionally, our system supports large messages thanks to the LargePayloads feature, enabling you to transmit data of any size with ease.

## Configuration

### LoRa

The LoRa configuration is done through the `loramesh/LoRaMeshService.cpp` file. When initializing you can change the default parameters, including the module of the device. See the [LoRaMesher](https://github.com/LoRaMesher/LoRaMesher) documentation for more information.

### WiFi

The WiFi configuration can be done with two ways. First of all, changing the default value of the `WIFI_SSID` and `WIFI_PASSWORD` variables in the `config.h` file. The second way is to use the `wifi` command in the Bluetooth Serial Terminal. When initializing the device it will show you the commands to introduce the WiFi credentials.

### MQTT

The MQTT you can change the `MQTT_SERVER` and `MQTT_PORT` variables in the `config.h` file. You can change the `MQTT_TOPIC_SUB` and `MQTT_TOPIC_OUT` variables to change the topic where the device will receive the messages and will publish the messages respectively.
To use MQTT you need to be connected to the WiFi network. There are other parameters described in the `config.h` file.

### Example on how to send a message to the device using MQTT:

- The topic is `from-server/1234`: `from-server` is the `MQTT_TOPIC_SUB` variable and `1234` is the destination device ID. However, you can specify the destination of the device using the `addrDst` field in the JSON message.

- You can look at the `message/dataMessage.h` file to see the different applications that are available. We are sending the message to the LED Application. Which in the appPort enum is the `LedApp`, that is the value `10`.

- Each application has a different payload or message. In this case, we are sending a message to the LED application. The payload is an object with the following field:

  - `ledCommand`: The command to execute. In this case, we are sending the `LedCommand::On` command, which is the value `1`.

- The message is sent in JSON format. The final JSON object is:

```json
{
  "dataMessage": {
    "appPortSrc": 10,
    "addrDst": 1234, // The destination device ID, Optional
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
    "appPortSrc": 9, // The Temperature Sensor App
    "addrDst": 1234, // The destination device ID, normally it will be the gateway responsible of sending the message to the server.
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

Be aware that when changing the board you need to change the module in the `loramesh/LoRaMeshService.cpp` file.

As we are using the Heltec WIFI LoRa 32 (V3) we needed to use a custom board configuration. [Here is why](http://community.heltec.cn/t/heltec-board-migration-from-v2-to-v3/12667).