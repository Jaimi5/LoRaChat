#include "mqttService.h"

/**
 * @brief Create a Bluetooth Task
 *
 */
void MqttService::createMqttTask() {
    int res = xTaskCreate(
        MqttLoop,
        "Mqtt Task",
        4096,
        (void*) 1,
        2,
        &mqtt_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Mqtt task handle error: %d"), res);
    }
}

void MqttService::MqttLoop(void*) {
    Log.traceln(F("Mqtt loop started"));
    MqttService& mqttService = MqttService::getInstance();

    for (;;) {
        mqttService.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

bool MqttService::isDeviceConnected() {
    return client.connected();
}

bool MqttService::writeToMqtt(DataMessage* message) {

    if (!isDeviceConnected()) {
        Log.warningln(F("No Mqtt device connected"));
        return false;
    }

    String json = MessageManager::getInstance().getJSON(message);

    if (json.length() > MQTT_MAX_PACKET_SIZE) {
        Log.errorln(F("Message too long"));
        return false;
    }

    memcpy(mqttMessageSend.body, json.c_str(), json.length() + 1);

    if (xQueueSend(sendQueue, (void*) &mqttMessageSend, (TickType_t) 10) != pdPASS) {
        Log.errorln(F("Error sending to queue"));
        return false;
    }

    return true;

    // bool sended = client.publish(MQTT_TOPIC_OUT, json.c_str());

    // If connected but not sended, try to reconnect and send?

    // return sended;
}

bool MqttService::writeToMqtt(String message) {
    Log.info(F("Sending message to mqtt: %s"), message);

    if (!isDeviceConnected()) {
        Log.warning(F("No Mqtt device connected"));
        return false;
    }
    // String json = MessageManager::getInstance().getJSON(message);
    client.publish("/hello", message);

    return true;
}

// void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
// {
//     BluetoothService &instance = MqttService::getInstance();
//     if (event == ESP_SPP_SRV_OPEN_EVT && instance.SerialBT->hasClient())
//     {
//         Log.verboseln("Bluetooth Connected");
//         String help = MessageManager::getInstance().getAvailableCommands();
//         Serial.println(help);
//         instance.writeToBluetooth(help);
//     }
//     else if (event == ESP_SPP_CLOSE_EVT && !instance.SerialBT->hasClient())
//     {
//         Log.verboseln("Bluetooth Disconnected");
//     }
// }

void callback(String& topic, String& payload) {
    Serial.println("incoming: " + topic + " - " + payload);

    // Note: Do not use the client in the callback to publish, subscribe or
    // unsubscribe as it may cause deadlocks when other things arrive while
    // sending and receiving acknowledgments. Instead, change a global variable,
    // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void MqttService::initMqtt(String lclName) {
    sendQueue = xQueueCreate(MQTT_MAX_QUEUE_SIZE, MQTT_MAX_PACKET_SIZE);

    // if (SerialBT->register_callback(callback) == ESP_OK) {
    //     Log.infoln(F("Bluetooth callback registered"));
    // }
    // else {
    //     Log.errorln(F("Bluetooth callback not registered"));
    // }

    // if (!SerialBT->begin(lclName)) {
    //     Log.errorln("BT init error");
    // }

    localName = lclName;

    Log.verboseln(F("DeviceID: %s"), lclName);

    Log.infoln(F("Free ram before starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // do not verify tls certificate
    // check the following example for methods to verify the server:
    // https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiClientSecure/examples/WiFiClientSecure/WiFiClientSecure.ino
    // net.setInsecure();
    // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported
    // by Arduino. You need to set the IP address directly.
    client.begin(MQTT_SERVER, MQTT_PORT, net);

    // we should set the keep alive interval to a greater value than the default
    // client.setKeepAlive(20);

    client.onMessage(callback);

    connect();

    createMqttTask();

    Log.infoln(F("Free ram after starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void MqttService::loop() {
    client.loop();

    vTaskDelay(10 / portTICK_PERIOD_MS);

    if (!client.connected()) {
        connect();
    }

    if (!client.connected())
        return;

    if (xQueueReceive(sendQueue, (void*) &mqttMessageReceive, 0) == pdTRUE) {
        Log.traceln(F("Sending message to mqtt"));
        client.publish(MQTT_TOPIC_OUT, mqttMessageReceive.body);
    }

    // publish a message roughly every second.
    if (millis() - lastMillis > 10000) {
        Log.traceln(F("Sending message to mqtt"));
        lastMillis = millis();
        client.publish(MQTT_TOPIC_OUT, "world");
    }
}

void MqttService::connect() {

    Serial.print("checking wifi...");
    WiFiServerService::getInstance().connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
        Log.infoln(F("Wifi not connected"));
        return;
    }

    Log.errorln(F("Wifi connected"));

    Serial.print("Connecting MQTT...");

    // TODO: Add username and password
    int retries = 0;
    while (!client.connect(localName.c_str()) && retries < 5) {
        Serial.print(".");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        retries++;
    }

    Serial.println("\nconnected!");

    // TODO: When routing table update notification, update the subscriptions accordingly
    // TODO: Or when sending a message, add an attribute to send to an specific node
    if (client.subscribe(MQTT_TOPIC_SUB)) {
        Log.infoln(F("Subscribed to topic %s"), MQTT_TOPIC_SUB);
    }
    else {
        Log.errorln(F("Error subscribing to topic %s"), MQTT_TOPIC_SUB);
    }
}

void MqttService::processReceivedMessage(messagePort port, DataMessage* message) {
    MqttMessage* mqttMessage = (MqttMessage*) message;
    switch (mqttMessage->type) {
        case MqttMessageType::mqttMessage:
            writeToMqtt(Helper::uint8ArrayToString(mqttMessage->message, mqttMessage->getPayloadSize()));
            break;
        default:
            break;
    }
}