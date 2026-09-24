#include "mqttService.h"

static const char* MQTT_TAG = "MQTT";

void MqttService::initMqtt(String lclName) {
    ESP_LOGI(MQTT_TAG, "Initializing mqtt");

    initialized = true;

    localName = lclName;

    mqtt_service_init(lclName.c_str());

    receiveQueue = xQueueCreate(10, sizeof(MQTTQueueMessageV2*));

    createMqttTask();

    // Set the MQTT_CLIENT library logging level
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_WARN);

    // Silence the TLS/transport reconnect spam emitted every retry while the
    // broker is unreachable; this UART/log-mutex traffic is what starves the
    // LoRa tasks. The MQTT event handler still logs disconnect/error events.
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);

    ESP_LOGI(MQTT_TAG, "Mqtt initialized");
}


static esp_mqtt_client_handle_t client;
bool mqtt_connected = false;
static int mqtt_error_count = 0;

void MqttService::createMqttTask() {
    int res = xTaskCreate(MqttLoop, "Mqtt Task", 4096, (void*)1, 2, &mqtt_TaskHandle);
    if (res != pdPASS)
        ESP_LOGE(MQTT_TAG, "Mqtt task handle error: %d", res);
}

void MqttService::MqttLoop(void*) {
    ESP_LOGV(MQTT_TAG, "Mqtt loop started");
    MqttService& mqttService = MqttService::getInstance();

    for (;;) {
        ESP_LOGV(MQTT_TAG, "Stack space unused after entering the task: %d",
                 uxTaskGetStackHighWaterMark(NULL));

        mqttService.processMQTTMessage();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void MqttService::processMQTTMessage() {
    if (xQueueReceive(receiveQueue, &mqttMessageReceiveV2, portMAX_DELAY) == pdTRUE) {
        ESP_LOGV(MQTT_TAG, "Message received from mqtt queue");
        ESP_LOGV(MQTT_TAG, "Topic: %s", mqttMessageReceiveV2->topic.c_str());

        processReceivedMessageFromMQTT(mqttMessageReceiveV2->topic, mqttMessageReceiveV2->body);

        delete mqttMessageReceiveV2;
    }
}

bool MqttService::sendMqttMessage(MQTTQueueMessageV2* message) {
    ESP_LOGV(MQTT_TAG, "Sending message to MQTT, topic %s", message->topic.c_str());
    mqtt_service_send(message->topic.c_str(), message->body.c_str(), 0);

    return true;
}

bool MqttService::connect() {
    if (!isInitialized()) {
        ESP_LOGW(MQTT_TAG, "Mqtt not initialized");
        return false;
    }

    if (!WiFiServerService::getInstance().connectWiFi()) {
        ESP_LOGW(MQTT_TAG, "No WiFi connection");
        return false;
    }

    if (isDeviceConnected())
        return true;

    esp_mqtt_client_start(client);

    // Do not block the caller waiting for the connection. esp_mqtt reconnects
    // on its own task and flips mqtt_connected via the event handler; busy-
    // waiting here (up to MAX_CONNECTION_TRY seconds) ran on the caller — incl.
    // the LoRa receive/forward path — and stalled time-critical work whenever
    // the broker was unreachable. Callers fall back to the LoRa mesh gateway
    // when this returns false (see MessageManager::sendMessageMqtt).
    return isDeviceConnected();
}

void MqttService::disconnect() {
    esp_mqtt_client_stop(client);

    mqtt_connected = false;
}

bool MqttService::isDeviceConnected() {
    return mqtt_connected;
}

bool MqttService::writeToMqtt(DataMessage* message) {
    if (!connect()) {
        ESP_LOGW(MQTT_TAG, "No Mqtt device connected");
        return false;
    }

    String json = MessageManager::getInstance().getJSON(message);

    MQTTQueueMessageV2* mqttMessageSend = new MQTTQueueMessageV2();

    mqttMessageSend->body = json;
    mqttMessageSend->topic = String(MQTT_TOPIC_OUT) + String(message->addrSrc);

    sendMqttMessage(mqttMessageSend);

    delete mqttMessageSend;

    return true;
}

bool MqttService::writeToMqtt(String message) {
    return false;
}

void MqttService::processReceivedMessageFromMQTT(String& topic, String& payload) {
    ESP_LOGI(MQTT_TAG, "Message arrived on topic: %s", topic.c_str());
    DataMessage* message = MessageManager::getInstance().getDataMessage(payload);

    if (message == NULL) {
        ESP_LOGE(MQTT_TAG, "Error parsing message");
        return;
    }

    if (message->addrDst == 0) {
        String getDst = topic.substring(topic.lastIndexOf("/") + 1);
        message->addrDst = getDst.toInt();

        if (message->addrDst == 0) {
            ESP_LOGE(MQTT_TAG, "Error parsing destination address");
            delete message;
            return;
        }
    }

    MessageManager::getInstance().processReceivedMessage(messagePort::MqttPort, message);

    ESP_LOGI(MQTT_TAG, "Message sent to services");

    delete message;
}

void MqttService::processReceivedMessage(messagePort port, DataMessage* message) {
    // TODO: Add some checks?
    writeToMqtt(message);
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id,
                               void* event_data) {
    ESP_LOGV(MQTT_TAG, "MQTT event handler");
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected = true;
            mqtt_error_count = 0;
            String topic = String(MQTT_TOPIC_SUB) + MqttService::getInstance().localName;
            esp_mqtt_client_subscribe(client, topic.c_str(), 2);
        } break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA: {
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");
            MqttService& mqttService = MqttService::getInstance();
            mqttService.process_message(event->topic, event->topic_len, event->data, event->data_len);
        } break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(MQTT_TAG, "Last errno string (%s)",
                         strerror(event->error_handle->esp_transport_sock_errno));
            }
            if (WiFiServerService::getInstance().isConnected() && mqtt_connected) {
                mqtt_connected = false;
                mqtt_error_count++;
                if (mqtt_error_count >= 3) {
                    ESP_LOGI(MQTT_TAG, "MQTT restart (rebooting) after %d errors", mqtt_error_count);
                    esp_restart();
                } else {
                    ESP_LOGI(MQTT_TAG, "MQTT error #%d — reconnecting (no reboot)", mqtt_error_count);
                    esp_mqtt_client_reconnect(client);
                }
            }
            break;
        default:
            // ESP_LOGI(MQTT_TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void MqttService::mqtt_app_start(const char* client_id) {
    String uri = "mqtt://" + String(MQTT_SERVER) + ":" + String(MQTT_PORT);

    ESP_LOGI(MQTT_TAG, "MQTT URI: %s", uri.c_str());

    esp_mqtt_client_config_t mqtt_cfg = {};

    mqtt_cfg.uri = uri.c_str();
    mqtt_cfg.client_id = client_id;
    mqtt_cfg.buffer_size = 2048;

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example
     * mqtt_event_handler */
    esp_mqtt_client_register_event(client, esp_mqtt_event_id_t::MQTT_EVENT_ANY, mqtt_event_handler,
                                   NULL);


    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}

void MqttService::mqtt_service_init(const char* client_id) {
    mqtt_app_start(client_id);
}

void MqttService::mqtt_service_subscribe(const char* topic) {
    esp_mqtt_client_subscribe(client, topic, 2);
    ESP_LOGI(MQTT_TAG, "Subscribed to topic %s", topic);
}

void MqttService::mqtt_service_send(const char* topic, const char* data, int len) {
    int msg_id;
    msg_id = esp_mqtt_client_publish(client, topic, data, len, 1, 0);
    if (msg_id == -1) {
        ESP_LOGE(MQTT_TAG, "Error sending message to MQTT");
        return;
    }
    ESP_LOGI(MQTT_TAG, "sent publish successful, msg_id %d", msg_id);
}

void MqttService::process_message(const char* topic, int topic_len, const char* payload, int data_len) {
    String topicStr = String(topic, topic_len);
    String payloadStr = String(payload, data_len);

    MQTTQueueMessageV2* mqttMessageReceive = new MQTTQueueMessageV2();
    mqttMessageReceive->topic = topicStr;
    mqttMessageReceive->body = payloadStr;

    if (xQueueSend(receiveQueue, &mqttMessageReceive, portMAX_DELAY) != pdPASS) {
        // ESP_LOGE(MQTT_TAG, "Error sending to queue");
        delete mqttMessageReceive;
        return;
    }
}
