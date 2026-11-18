#include "commandRouter.h"

#include <memory>
#include <new>
#include <vector>

#include "deploymentKey.h"
#include "esp_timer.h"
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
/** Time between two looks at the forwarded commands that wait for a reply. */
static constexpr uint32_t SWEEP_MS = 1000;

/** @return milliseconds since boot. The clock wraps after 49 days. */
static uint32_t clockMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void CommandRouter::begin() {
    if (!queue_.create(QUEUE_LENGTH) ||
        xTaskCreate(task, "Commands", TASK_STACK, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(CR_TAG, "Command task could not be started");
    }
}

bool CommandRouter::submit(const CommandRequest& request) {
    if (!queue_.valid()) return false;
    auto item = std::unique_ptr<CommandRequest>(new (std::nothrow) CommandRequest(request));
    if (!item) return false;
    if (!queue_.send(item, 0)) {
        ESP_LOGW(CR_TAG, "Command queue full, line dropped");
        return false;
    }
    return true;
}

void CommandRouter::task(void* parameter) {
    auto* self = static_cast<CommandRouter*>(parameter);
    for (;;) {
        // The task only wakes by the clock while a forwarded command waits for its reply.
        TickType_t wait = self->pending_.size() > 0 ? pdMS_TO_TICKS(SWEEP_MS) : portMAX_DELAY;
        std::unique_ptr<CommandRequest> request = self->queue_.receive(wait);
        if (request) self->handle(*request);
        self->sweepPending();
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
    request.command.line = frame.line;
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
        PendingCommand pending;
        if (pending_.take(request.src, request.requestId, pending)) {
            deliver(pending, request.command.line.c_str());
        } else {
            // Late, repeated or never asked for.
            ESP_LOGW(CR_TAG, "Unexpected reply from %04X [%u]", request.src, request.requestId);
        }
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
    std::string text = runOncePerCounter(
        counter_, request.command.counter,
        [this](uint32_t counter) { return storeCounter(counter); },
        [&request]() {
            String reply = MessageManager::getInstance().executeCommand(
                request.command.line.c_str(), request.origin, true);
            return std::string(reply.c_str());
        });
    return String(text.c_str());
}

void CommandRouter::forward(const CommandRequest& request) {
    CommandFrame frame;
    frame.line = request.command.line;
    frame.isSigned = request.command.isSigned;
    frame.counter = request.command.counter;
    frame.tag = request.command.tag;

    PendingCommand pending;
    pending.dst = request.command.dst;
    pending.origin = request.origin;
    pending.originRequestId = request.requestId;
    pending.isSigned = frame.isSigned;
    pending.frame = frame.encode();

    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    size_t maxFrame = mesh.maxAppPayload();
    if (pending.frame.size() > maxFrame) {
        reply(request, String("Too long for one LoRa frame (max ") + maxFrame + " B)");
        return;
    }
    if (!mesh.hasRouteTo(pending.dst)) {
        reply(request, "ERR no route");
        return;
    }

    pending.requestId = pending_.nextRequestId();
    pending.timeoutMs = PendingCommands::timeoutMs(mesh.getSuperframeDurationMs());
    PendingCommand evicted;
    if (pending_.add(pending, clockMs(), evicted)) deliver(evicted, "ERR dropped");
    sendFrame(pending.dst, pending.requestId, pending.frame);
    if (request.origin == Origin::SERIAL_CONSOLE) {
        reply(request,
              String("Sent to ") + String(pending.dst, HEX) + " [" + pending.requestId + "]");
    }
}

void CommandRouter::deliver(const PendingCommand& pending, const String& text) {
    if (pending.origin == Origin::MQTT) {
        MqttService::getInstance().publishCommandReply(pending.dst, pending.originRequestId, text);
    } else {
        Serial.printf("< %04X [%u] %s\n", pending.dst, pending.requestId, text.c_str());
    }
}

void CommandRouter::sweepPending() {
    if (pending_.size() == 0) return;
    std::vector<PendingCommand> retransmit;
    std::vector<PendingCommand> expired;
    pending_.sweep(clockMs(), retransmit, expired);
    for (const PendingCommand& pending : retransmit) {
        ESP_LOGI(CR_TAG, "No reply from %04X [%u] yet, sending the command again", pending.dst,
                 pending.requestId);
        sendFrame(pending.dst, pending.requestId, pending.frame);
    }
    for (const PendingCommand& pending : expired) {
        deliver(pending, String("ERR timeout ") + (pending.timeoutMs / 1000) + " s");
    }
}

void CommandRouter::reply(const CommandRequest& request, const String& text) {
    if (request.origin == Origin::SERIAL_CONSOLE) {
        const std::string name = request.command.line.substr(0, request.command.line.find(' '));
        Serial.printf("> %s\n%s\n", name.c_str(), text.c_str());
        return;
    }
    if (request.origin == Origin::MQTT) {
        // The server pairs a reply with the node the command was for, also when the gateway
        // answers in its place.
        MqttService::getInstance().publishCommandReply(request.command.dst, request.requestId,
                                                       text);
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
        sendFrame(request.src, request.requestId, frame.encode());
    }
}

void CommandRouter::sendFrame(uint16_t dst, uint8_t requestId, const std::vector<uint8_t>& bytes) {
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
