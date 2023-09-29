#include "mqttService.h"

void MqttService::initMqtt(String lclName) {
    Log.verboseln(F("Initializing mqtt"));

    mqtt_service_init(lclName.c_str());

    sendQueue = xQueueCreate(10, sizeof(MQTTQueueMessage*));

    receiveQueue = xQueueCreate(10, sizeof(MQTTQueueMessageV2*));

    createMqttTask();

    Log.verboseln(F("Mqtt initialized"));
}


static esp_mqtt_client_handle_t client;
bool mqtt_connected = false;

void MqttService::createMqttTask() {
    int res = xTaskCreate(
        MqttLoop,
        "Mqtt Task",
        8196,
        (void*) 1,
        2,
        &mqtt_TaskHandle);
    if (res != pdPASS)
        Log.errorln(F("Mqtt task handle error: %d"), res);
}

void MqttService::MqttLoop(void*) {
    Log.traceln(F("Mqtt loop started"));
    MqttService& mqttService = MqttService::getInstance();

    for (;;) {
        mqttService.processMQTTMessage();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void MqttService::processMQTTMessage() {
    if (xQueueReceive(receiveQueue, &mqttMessageReceiveV2, 0) == pdTRUE) {
        // TODO: Process message
        Log.traceln(F("Message received from mqtt queue"));
        Log.traceln(F("Topic: %s"), mqttMessageReceiveV2->topic.c_str());

        processReceivedMessageFromMQTT(mqttMessageReceiveV2->topic, mqttMessageReceiveV2->body);

        delete mqttMessageReceiveV2;
    }
}

bool MqttService::sendMqttMessage(MQTTQueueMessageV2* message) {
    mqtt_service_send(message->topic.c_str(), message->body.c_str(), message->body.length());

    return true;
}

void MqttService::connect() {
    if (!WiFiServerService::getInstance().connectWiFi()) {
        Log.warningln(F("No WiFi connection"));
        return;
    }

    if (MqttService::getInstance().isDeviceConnected())
        return;

    esp_mqtt_client_start(client);

    Log.verbose(F("Waiting for MQTT connection to start"));

    while (!MqttService::getInstance().isDeviceConnected()) {
        Log.verbose(F("."));
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second
    }

}

void MqttService::disconnect() {
    if (!MqttService::getInstance().isDeviceConnected())
        return;

    esp_mqtt_client_stop(client);
}

bool MqttService::isDeviceConnected() {
    return mqtt_connected;
}

bool MqttService::writeToMqtt(DataMessage* message) {


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

    MQTTQueueMessageV2* mqttMessageSend = new MQTTQueueMessageV2();

    mqttMessageSend->body = json;
    mqttMessageSend->topic = String(MQTT_TOPIC_OUT) + String(message->addrSrc);

    Log.verboseln(F("Sending message to MQTT, topic %s"), mqttMessageSend->topic.c_str());

    sendMqttMessage(mqttMessageSend);

    delete mqttMessageSend;

    return true;
}

bool MqttService::writeToMqtt(String message) {
    return false;
}

void MqttService::processReceivedMessageFromMQTT(String& topic, String& payload) {

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

void MqttService::processReceivedMessage(messagePort port, DataMessage* message) {
    // TODO: Add some checks?
    writeToMqtt(message);
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    // Log.verboseln("Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            {
                mqtt_connected = true;
                // Log.verboseln("MQTT_EVENT_CONNECTED");
                esp_mqtt_client_subscribe(client, MQTT_TOPIC_SUB, 2);
                // MqttService& mqttService = MqttService::getInstance();
                // mqttService.mqtt_service_subscribe(MQTT_TOPIC_SUB);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            // Log.verboseln("MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            // Log.verboseln("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            // Log.verboseln("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            // Log.verboseln("MQTT_EVENT_PUBLISHED, msg_id=%d, topic=%s", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            {
                // Log.verboseln("MQTT_EVENT_DATA");
                MqttService& mqttService = MqttService::getInstance();
                mqttService.process_message(event->topic, event->data);
            }
            break;
        case MQTT_EVENT_ERROR:
            // Log.verboseln("MQTT_EVENT_ERROR");
            // if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            //     Log.verboseln("Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            // }
            break;
        default:
            // Log.verboseln("Other event id:%d", event->event_id);
            break;
    }
}

void MqttService::mqtt_app_start(const char* client_id) {
    String uri = "mqtt://" + String(MQTT_SERVER) + ":" + String(MQTT_PORT);

    Log.verboseln("MQTT URI: %s", uri.c_str());

    esp_mqtt_client_config_t mqtt_cfg = {0};  // initialize all fields to zero

    mqtt_cfg.uri = uri.c_str();
    mqtt_cfg.client_id = client_id;

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, esp_mqtt_event_id_t::MQTT_EVENT_ANY, mqtt_event_handler, NULL);


    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}

void MqttService::mqtt_service_init(const char* client_id) {
    mqtt_app_start(client_id);
}

void MqttService::mqtt_service_subscribe(const char* topic) {
    esp_mqtt_client_subscribe(client, topic, 2);
    Log.verboseln("Subscribed to topic %s", topic);
}

void MqttService::mqtt_service_send(const char* topic, const char* data, int len) {
    int msg_id;
    msg_id = esp_mqtt_client_publish(client, topic, data, len, 2, 0);
    Log.verboseln("sent publish successful, msg_id %d", msg_id);
}

void MqttService::process_message(const char* topic, const char* payload) {
    String topicStr = String(topic);
    String payloadStr = String(payload);

    MQTTQueueMessageV2* mqttMessageReceive = new MQTTQueueMessageV2();
    mqttMessageReceive->topic = topicStr;
    mqttMessageReceive->body = payloadStr;

    if (xQueueSend(receiveQueue, &mqttMessageReceive, portMAX_DELAY) != pdPASS) {
        // Log.errorln(F("Error sending to queue"));
        delete mqttMessageReceive;
        return;
    }
}
