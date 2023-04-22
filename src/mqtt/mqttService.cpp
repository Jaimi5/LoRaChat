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

bool MqttService::sendMqttMessage(MQTTQueueMessage* message) {
    return client->publish(MQTT_TOPIC_OUT + String(message->topic), message->body);
}

bool MqttService::isDeviceConnected() {
    return client->connected();
}

bool MqttService::writeToMqtt(DataMessage* message) {
    // TODO: Check for wifi connection, not mqtt connection
    if (!isDeviceConnected()) {
        Log.warningln(F("No Mqtt device connected"));
        return false;
    }

    String json = MessageManager::getInstance().getJSON(message);

    // TODO: Need to find the correct number but this is a good start
    uint16_t length = json.length() + 1 + sizeof(lwmqtt_message_t);

    Log.verboseln(F("Message length: %d"), length);

    if (length > MQTT_MAX_PACKET_SIZE) {
        Log.errorln(F("Message too long"));
        return false;
    }

    MQTTQueueMessage* mqttMessageSend = new MQTTQueueMessage();

    memcpy(mqttMessageSend->body, json.c_str(), json.length() + 1);
    mqttMessageSend->topic = message->addrSrc;

    if (xQueueSend(sendQueue, &mqttMessageSend, (TickType_t) 10) != pdPASS) {
        Log.errorln(F("Error sending to queue"));
        delete mqttMessageSend;
        return false;
    }

    return true;
}

bool MqttService::writeToMqtt(String message) {
    Log.info(F("Sending message to mqtt: %s"), message);

    if (!isDeviceConnected()) {
        Log.warning(F("No Mqtt device connected"));
        return false;
    }
    // String json = MessageManager::getInstance().getJSON(message);
    client->publish("/hello", message);

    return true;
}

void callback(String& topic, String& payload) {

    Log.infoln(F("Message arrived on topic: %s"), topic.c_str());
    DataMessage* message = MessageManager::getInstance().getDataMessage(payload);

    if (message == NULL) {
        Log.errorln(F("Error parsing message"));
        return;
    }

    if (message->addrDst == 0) {
        String getDst = topic.substring(topic.lastIndexOf("/") + 1);
        message->addrDst = getDst.toInt();

        if (message->addrDst == 0) {
            Log.errorln(F("Error parsing destination address"));
            delete message;
            return;
        }
    }

    MessageManager::getInstance().processReceivedMessage(messagePort::MqttPort, message);

    Log.verboseln(F("Message sent to services"));

    delete message;
}

void MqttService::initMqtt(String lclName) {
    sendQueue = xQueueCreate(MQTT_MAX_QUEUE_SIZE, sizeof(MQTTQueueMessage*));

    if (sendQueue == NULL) {
        Log.errorln(F("Error creating queue"));
    }

    localName = lclName;

    Log.verboseln(F("DeviceID: %s"), lclName);

    Log.infoln(F("Free ram before starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // do not verify tls certificate
    // check the following example for methods to verify the server:
    // https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiClientSecure/examples/WiFiClientSecure/WiFiClientSecure.ino
    net.setInsecure();
    // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported
    // by Arduino. You need to set the IP address directly.
    client->begin(MQTT_SERVER, MQTT_PORT, net);

    // we should set the keep alive interval to a greater value than the default
    // client->setKeepAlive(20);

    client->onMessage(callback);

    connect();

    createMqttTask();

    Log.infoln(F("Free ram after starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void MqttService::loop() {

    // TODO: If we can connect to wifi multiple times we should remove this service

    client->loop();

    vTaskDelay(10 / portTICK_PERIOD_MS);

    if (!client->connected()) {
        connect();
    }

    if (!client->connected()) {
        wifiRetries++;
        // TODO: This should be different, try reconnect every x minutes if wifi available?
        if (wifiRetries > MAX_CONNECTION_TRY) {
            Log.errorln(F("Removing mqtt service"));
            vTaskDelete(NULL);
            return;
        }
        return;
    }

    wifiRetries = 0;

    if (xQueueReceive(sendQueue, &mqttMessageReceive, 0) == pdTRUE) {
        Log.traceln(F("Sending message to mqtt queue"));
        if (sendMqttMessage(mqttMessageReceive)) {
            Log.traceln(F("Queue message sent"));
            delete mqttMessageReceive;
        }
        else {
            if (xQueueSendToFront(sendQueue, &mqttMessageReceive, (TickType_t) 0) != pdPASS) {
                delete mqttMessageReceive;
                Log.errorln(F("Error sending message to mqtt"));
            }
        }
    }

    // publish a message roughly every second.
    if (millis() - lastMillis > 20000) {
        Log.traceln(F("Sending message to mqtt"));
        lastMillis = millis();
        client->publish(MQTT_TOPIC_OUT + localName, "Since boot: " + String(millis() / 1000));
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
    while (!client->connect(localName.c_str(), MQTT_USERNAME, MQTT_PASSWORD) && retries < 5) {
        Serial.print(".");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        retries++;
    }

    Serial.println("\nconnected!");

    // TODO: When routing table update notification, update the subscriptions accordingly
    // TODO: Or when sending a message, add an attribute to send to an specific node
    if (client->subscribe(MQTT_TOPIC_SUB)) {
        Log.infoln(F("Subscribed to topic %s"), MQTT_TOPIC_SUB);
    }
    else {
        Log.errorln(F("Error subscribing to topic %s"), MQTT_TOPIC_SUB);
    }
}

void MqttService::processReceivedMessage(messagePort port, DataMessage* message) {
    // TODO: Add some checks?
    writeToMqtt(message);
}