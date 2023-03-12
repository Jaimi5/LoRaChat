#include "mqttService.h"

/**
 * @brief Create a Bluetooth Task
 *
 */
void MqttService::createMqttTask()
{
    int res = xTaskCreate(
        MqttLoop,
        "Mqtt Task",
        4096,
        (void *)1,
        2,
        &mqtt_TaskHandle);
    if (res != pdPASS)
    {
        Log.errorln(F("Mqtt task handle error: %d"), res);
    }
}

void MqttService::MqttLoop(void *)
{
    MqttService &mqttService = MqttService::getInstance();
    for (;;)
    {
        mqttService.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

bool MqttService::isDeviceConnected()
{
    return client.connected();
}

bool MqttService::writeToMqtt(DataMessage *message)
{
    Log.info(F("Sending message to mqtt: %s"), message->message);

    if (!isDeviceConnected())
    {
        Log.warning(F("No Mqtt device connected"));
        return false;
    }
    // String json = MessageManager::getInstance().getJSON(message);
    client.publish("/hello", "world");

    return true;
}

bool MqttService::writeToMqtt(String message)
{
    Log.info(F("Sending message to mqtt: %s"), message);

    if (!isDeviceConnected())
    {
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

void callback(String &topic, String &payload)
{
    Serial.println("incoming: " + topic + " - " + payload);

    // Note: Do not use the client in the callback to publish, subscribe or
    // unsubscribe as it may cause deadlocks when other things arrive while
    // sending and receiving acknowledgments. Instead, change a global variable,
    // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void MqttService::initMqtt(String lclName)
{
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
    net.setInsecure();
    // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported
    // by Arduino. You need to set the IP address directly.
    client.begin("carmelofiorello.com", 8883, net);
    client.onMessage(callback);

    Serial.print("checking wifi...");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        // delay(1000);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    Serial.print("\nconnecting...");
    while (!client.connect("joan", "joan", "joan1234"))
    {
        Serial.print(".");
        // delay(1000);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    Serial.println("\nconnected!");

    client.subscribe("/toLora");

    createMqttTask();

    Log.infoln(F("Free ram after starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void MqttService::loop()
{
    // while (SerialBT->available())
    // {
    //     String message = SerialBT->readStringUntil('\n');
    //     message.remove(message.length() - 1, 1);
    //     Serial.println(message);
    //     String executedProgram = MessageManager::getInstance().executeCommand(message);
    //     Serial.println(executedProgram);
    //     writeToBluetooth(executedProgram);
    // }
    client.loop();
    // delay(10); // <- fixes some issues with WiFi stability
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // if (!client.connected())
    // {
    //     connect();
    // }

    // publish a message roughly every second.
    if (millis() - lastMillis > 10000)
    {
        lastMillis = millis();
        client.publish("/fromLora", "world");
    }
}

void MqttService::processReceivedMessage(messagePort port, DataMessage *message)
{
    MqttMessage *mqttMessage = (MqttMessage *)message;
    switch (mqttMessage->type)
    {
    case MqttMessageType::mqttMessage:
        writeToMqtt(Helper::uint8ArrayToString(mqttMessage->message, mqttMessage->getPayloadSize()));
        break;
    default:
        break;
    }
}