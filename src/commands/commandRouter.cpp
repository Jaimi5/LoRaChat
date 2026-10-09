#include "commandRouter.h"

#include <memory>
#include <vector>

#include "deploymentKey.h"
#include "loramesh/loraMeshService.h"
#include "message/messageManager.h"
#include "mqtt/mqttService.h"
#include "nvs.h"
#include "ota/otaService.h"

static const char* CR_TAG = "CommandRouter";
static const char* NVS_NAMESPACE = "lmsec";
static const char* NVS_KEY_COUNTER = "ctr";
static constexpr uint32_t TASK_STACK = 8192;
static constexpr UBaseType_t QUEUE_LENGTH = 8;
static const char TRUNCATED_MARK = '~';

void CommandRouter::begin() {
    queue_ = xQueueCreate(QUEUE_LENGTH, sizeof(CommandRequest*));
    if (queue_ == nullptr ||
        xTaskCreate(task, "Commands", TASK_STACK, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(CR_TAG, "Command task could not be started");
    }
}

bool CommandRouter::submit(const CommandRequest& request) {
    if (queue_ == nullptr) return false;
    CommandRequest* item = new (std::nothrow) CommandRequest(request);
    if (item == nullptr) return false;
    if (xQueueSend(queue_, &item, 0) != pdTRUE) {
        delete item;
        ESP_LOGW(CR_TAG, "Command queue full, line dropped");
        return false;
    }
    return true;
}

void CommandRouter::task(void* parameter) {
    auto* self = static_cast<CommandRouter*>(parameter);
    for (;;) {
        CommandRequest* item = nullptr;
        if (xQueueReceive(self->queue_, &item, portMAX_DELAY) != pdTRUE) continue;
        std::unique_ptr<CommandRequest> request(item);
        self->handle(*request);
    }
}

void CommandRouter::processReceivedMessage(messagePort port, DataMessage* message) {
    CommandFrame frame;
    if (!CommandFrame::decode(message->message, message->messageSize, frame)) {
        ESP_LOGW(CR_TAG, "Malformed command frame from %04X", message->addrSrc);
        return;
    }
    CommandRequest request;
    request.origin = Origin::LORA;
    request.src = message->addrSrc;
    request.requestId = message->messageId;
    request.response = frame.response;
    request.command.line = frame.line.c_str();
    request.command.isSigned = frame.isSigned;
    request.command.counter = frame.counter;
    request.command.tag = frame.tag;
    submit(request);
}

bool CommandRouter::runningCounter(uint32_t& out) const {
    if (running_ == nullptr || !running_->command.isSigned) return false;
    out = running_->command.counter;
    return true;
}

void CommandRouter::handle(const CommandRequest& request) {
    if (request.response) {
        for (Pending& pending : pending_) {
            if (pending.used && pending.dst == request.src &&
                pending.requestId == request.requestId) {
                pending.used = false;
                if (pending.origin == Origin::MQTT) {
                    MqttService::getInstance().publishCommandReply(
                        request.src, pending.originRequestId, request.command.line.c_str());
                } else {
                    Serial.printf("< %04X [%u] %s\n", request.src, request.requestId,
                                  request.command.line.c_str());
                }
                return;
            }
        }
        ESP_LOGW(CR_TAG, "Unexpected reply from %04X [%u]", request.src, request.requestId);
        return;
    }

    uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
    if (request.command.remote && request.command.dst != local) {
        forward(request);
        return;
    }
    running_ = &request;
    String text = request.command.isSigned && request.origin != Origin::SERIAL_CONSOLE
                      ? runSigned(request)
                      : runLocal(request);
    running_ = nullptr;
    reply(request, text);
}

String CommandRouter::runLocal(const CommandRequest& request) {
    return MessageManager::getInstance().executeCommand(request.command.line.c_str(),
                                                        request.origin, false);
}

String CommandRouter::runSigned(const CommandRequest& request) {
    DeploymentKey key;
    if (!OtaService::getInstance().deploymentKey(key)) return "ERR auth";
    KeyedHmac hmac = [&key](const uint8_t* data, size_t size) {
        return hmacSha256(key.data(), key.size(), data, size);
    };
    uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
    CommandTag expected =
        commandTag(hmac, local, request.command.counter, request.command.line);
    if (!commandTagsEqual(expected, request.command.tag)) return "ERR auth";

    if (!counterLoaded_ && !loadCounter()) return "ERR storage";
    switch (counter_.decide(request.command.counter)) {
        case CounterDecision::REPLAY:
            return "ERR replay";
        case CounterDecision::REPEAT:
            return counter_.lastReply().c_str();
        case CounterDecision::EXECUTE:
            break;
    }
    // The counter is stored before the command runs, so it never runs twice.
    if (!storeCounter(request.command.counter)) return "ERR storage";
    counter_.accept(request.command.counter);
    String text = MessageManager::getInstance().executeCommand(request.command.line.c_str(),
                                                               request.origin, true);
    counter_.setReply(text.c_str());
    return text;
}

void CommandRouter::forward(const CommandRequest& request) {
    CommandFrame frame;
    frame.line = request.command.line;
    frame.isSigned = request.command.isSigned;
    frame.counter = request.command.counter;
    frame.tag = request.command.tag;
    uint8_t requestId = ++nextRequestId_;
    Pending& slot = pending_[nextPending_];
    nextPending_ = (nextPending_ + 1) % pending_.size();
    slot = {true, request.command.dst, requestId, request.origin, request.requestId};

    size_t maxFrame = LoRaMeshService::getInstance().maxAppPayload();
    if (frame.encode().size() > maxFrame) {
        slot.used = false;
        reply(request, String("Too long for one LoRa frame (max ") + maxFrame + " B)");
        return;
    }
    sendFrame(request.command.dst, requestId, frame);
    if (request.origin == Origin::SERIAL_CONSOLE) {
        reply(request,
              String("Sent to ") + String(request.command.dst, HEX) + " [" + requestId + "]");
    }
}

void CommandRouter::reply(const CommandRequest& request, const String& text) {
    if (request.origin == Origin::SERIAL_CONSOLE) {
        String name = request.command.line.substr(0, request.command.line.find(' ')).c_str();
        Serial.printf("> %s\n%s\n", name.c_str(), text.c_str());
        return;
    }
    if (request.origin == Origin::MQTT) {
        uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
        MqttService::getInstance().publishCommandReply(local, request.requestId, text);
        return;
    }
    if (request.origin == Origin::LORA) {
        CommandFrame frame;
        frame.response = true;
        frame.line = text.c_str();
        size_t maxLine = LoRaMeshService::getInstance().maxAppPayload() - 1;
        if (frame.line.size() > maxLine) {
            frame.line.resize(maxLine - 1);
            frame.line += TRUNCATED_MARK;
        }
        sendFrame(request.src, request.requestId, frame);
    }
}

void CommandRouter::sendFrame(uint16_t dst, uint8_t requestId, const CommandFrame& frame) {
    std::vector<uint8_t> bytes = frame.encode();
    std::vector<uint8_t> buffer(sizeof(DataMessage) + bytes.size());
    DataMessage* message = reinterpret_cast<DataMessage*>(buffer.data());
    message->appPortDst = CommandApp;
    message->appPortSrc = CommandApp;
    message->messageId = requestId;
    message->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    message->addrDst = dst;
    message->messageSize = bytes.size();
    std::copy(bytes.begin(), bytes.end(), message->message);
    MessageManager::getInstance().sendMessage(LoRaMeshPort, message);
}

bool CommandRouter::loadCounter() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        counterLoaded_ = true;
        return true;
    }
    if (err != ESP_OK) return false;
    uint32_t last = 0;
    err = nvs_get_u32(handle, NVS_KEY_COUNTER, &last);
    nvs_close(handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return false;
    counter_ = CommandCounter(last, err == ESP_OK);
    counterLoaded_ = true;
    return true;
}

bool CommandRouter::storeCounter(uint32_t counter) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u32(handle, NVS_KEY_COUNTER, counter);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(CR_TAG, "Cannot store the command counter: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}
