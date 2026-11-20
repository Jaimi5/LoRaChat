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

CommandRouter::CommandRouter() : MessageService(CommandApp, "Command") {
    commandService = &commands_;
    commands_.addCommand(Command("/help", "List the commands", Perm::OPEN, [this](String) {
        return MessageManager::getInstance().help(runningOrigin());
    }));
    commands_.addCommand(
        Command("/cmds", "List every command for programs, in pages", Perm::OPEN, [](String args) {
            return MessageManager::getInstance().catalogue(args);
        }).withUsage("[page]"));
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
    CommandRequest request;
    request.origin = Origin::LORA;
    request.src = message->addrSrc;
    request.requestId = message->messageId;
    if (isBinaryCommandFrame(message->message, message->messageSize)) {
        BinaryCommandFrame binary;
        if (!BinaryCommandFrame::decode(message->message, message->messageSize, binary)) {
            ESP_LOGW(CR_TAG, "Malformed binary command frame from %04X", message->addrSrc);
            return;
        }
        request.binary = true;
        request.response = binary.response;
        request.status = binary.status;
        request.replayed = binary.replayed;
        request.commandId = binary.id;
        request.payload = binary.payload;
        request.command.isSigned = binary.isSigned;
        request.command.counter = binary.counter;
        request.command.tag = binary.tag;
        submit(request);
        return;
    }
    CommandFrame frame;
    if (!CommandFrame::decode(message->message, message->messageSize, frame)) {
        ESP_LOGW(CR_TAG, "Malformed command frame from %04X", message->addrSrc);
        return;
    }
    request.response = frame.response;
    request.status = frame.status;
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

Origin CommandRouter::runningOrigin() const {
    return running_ != nullptr ? running_->origin : Origin::SERIAL_CONSOLE;
}

void CommandRouter::handle(const CommandRequest& request) {
    if (request.response) {
        handleReply(request);
        return;
    }
    if (request.cancel) {
        cancel(request);
        return;
    }

    if (request.binary) {
        serveBinary(request);
        return;
    }
    uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
    if (request.command.remote && request.command.dst != local) {
        forward(request);
        return;
    }
    running_ = &request;
    CommandReply result = request.command.isSigned && request.origin != Origin::SERIAL_CONSOLE
                              ? runSigned(request)
                              : runLocal(request);
    running_ = nullptr;
    reply(request, result);
}

void CommandRouter::handleReply(const CommandRequest& request) {
    uint32_t nowMs = clockMs();
    PendingCommand pending;
    if (!pending_.take(request.src, request.requestId, pending)) {
        if (!pending_.takeLate(request.src, request.requestId, nowMs, pending)) {
            // Repeated, older than the time late replies are kept, or never asked for.
            ESP_LOGW(CR_TAG, "Unexpected reply from %04X [%u]", request.src, request.requestId);
            return;
        }
        ESP_LOGI(CR_TAG, "Late reply from %04X [%u] after %u s", request.src, request.requestId,
                 static_cast<unsigned>(pending.waitedMs(nowMs) / 1000));
    }
    RttPath path;
    path.dst = pending.dst;
    path.hops = pending.timing.hops;
    path.nextHop = pending.nextHop;
    path.superframeMs = pending.timing.superframeMs;
    rtt_.addSample(path, pending.waitedMs(nowMs), pending.timing.slotWaitMs, pending.retransmitted);
    if (request.binary) {
        BinaryCommandFrame frame;
        frame.response = true;
        frame.status = request.status;
        frame.replayed = request.replayed;
        frame.payload = request.payload;
        const Command* command = MessageManager::getInstance().findCommand(pending.name.c_str());
        TypedDeclaration declaration;
        const bool declared = command != nullptr && command->declaration(declaration);
        deliver(pending, renderBinaryReply(frame, declared ? &declaration : nullptr, pending.args));
    } else {
        if (pending.binary) {
            // A text answer to a binary frame comes from a node that does not read them.
            ESP_LOGW(CR_TAG, "Node %04X answered a binary command as text", request.src);
            binaryNodes_.note(request.src, false);
        }
        deliver(pending, CommandReply(request.status, request.command.line.c_str()));
    }
    acknowledge(pending, formatDoneAck(pending, nowMs));
}

CommandReply CommandRouter::runLocal(const CommandRequest& request) {
    return MessageManager::getInstance().executeCommand(request.command.line.c_str(),
                                                        request.origin, false);
}

KeyedHmac CommandRouter::deploymentHmac() {
    DeploymentKey key;
    if (!OtaService::getInstance().deploymentKey(key)) return KeyedHmac();
    return [key](const uint8_t* data, size_t size) {
        return hmacSha256(key.data(), key.size(), data, size);
    };
}

void CommandRouter::serveBinary(const CommandRequest& request) {
    BinaryCommandFrame frame;
    frame.id = request.commandId;
    frame.isSigned = request.command.isSigned;
    frame.counter = request.command.counter;
    frame.tag = request.command.tag;
    frame.payload = request.payload;

    ServedCommand served;
    const Command* command = MessageManager::getInstance().findCommandById(frame.id);
    const bool known = command != nullptr && command->declaration(served.declaration);
    if (known) {
        served.name = command->getCommand().c_str();
        served.perm = command->getPerm();
        // The handler runs as the command of this request, for what it reads from the router.
        served.handler = [this, command, &request](const Fields& in, Fields& out) {
            running_ = &request;
            const TypedResult result = command->typedHandler()(in, out);
            running_ = nullptr;
            return result;
        };
        if (!command->typedHandler()) served.handler = nullptr;
    }
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    NodeAuth auth;
    auth.address = mesh.getLocalAddress();
    if (frame.isSigned) auth.hmac = deploymentHmac();
    auth.counter = [this]() { return counterLoaded_ || loadCounter() ? &counter_ : nullptr; };
    auth.store = [this](uint32_t counter) { return storeCounter(counter); };
    const BinaryCommandFrame reply = serveBinaryCommand(frame, known ? &served : nullptr,
                                                        request.origin, auth, mesh.maxAppPayload());
    if (sendFrame(request.src, request.requestId, reply.encode()) != CommandStatus::OK) {
        ESP_LOGW(CR_TAG, "The reply to %04X [%u] could not be sent", request.src,
                 request.requestId);
    }
}

CommandReply CommandRouter::runSigned(const CommandRequest& request) {
    KeyedHmac hmac = deploymentHmac();
    if (!hmac) return CommandStatus::AUTH;
    uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
    CommandTag expected =
        commandTag(hmac, local, request.command.counter, request.command.line);
    if (!commandTagsEqual(expected, request.command.tag)) return CommandStatus::AUTH;

    if (!counterLoaded_ && !loadCounter()) return CommandStatus::STORAGE;
    return runOncePerCounter(
        counter_, request.command.counter,
        [this](uint32_t counter) { return storeCounter(counter); },
        [&request]() {
            return MessageManager::getInstance()
                .executeCommand(request.command.line.c_str(), request.origin, true)
                .result();
        });
}

AttemptTiming CommandRouter::currentTiming(const CommandRoute& route, uint32_t allowanceMs) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    AttemptTiming timing;
    timing.superframeMs = mesh.getSuperframeDurationMs();
    timing.allowanceMs = allowanceMs;
    timing.hops = route.hops;
    // Without a known slot the wait counts as a whole superframe.
    if (!mesh.timeToNextTxSlotMs(timing.slotWaitMs)) timing.slotWaitMs = timing.superframeMs;
    return timing;
}

uint32_t CommandRouter::expectedIn(uint16_t dst, uint16_t nextHop, const AttemptTiming& timing) {
    RttPath path;
    path.dst = dst;
    path.hops = timing.hops;
    path.nextHop = nextHop;
    path.superframeMs = timing.superframeMs;
    uint32_t srttMs = 0;
    bool measured = rtt_.estimate(path, srttMs);
    return expectedReplyMs(timing, measured, srttMs);
}

void CommandRouter::forward(const CommandRequest& request) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    std::string name;
    std::string args;
    splitCommand(request.command.line, name, args);
    const Command* command = MessageManager::getInstance().findCommand(name.c_str());
    CommandSpec spec;
    if (command != nullptr) {
        spec = command->spec();
        // A word that stands for a value is replaced only in a line without a signature.
        if (!request.command.isSigned) args = command->typedArgs(args.c_str()).c_str();
    }
    ForwardPlan plan = planForward(request.command, args, command != nullptr ? &spec : nullptr,
                                   binaryNodes_.has(request.command.dst), mesh.maxAppPayload());
    if (!plan.send()) {
        reply(request, CommandReply(plan.status, plan.argument.c_str()));
        return;
    }

    PendingCommand pending;
    pending.dst = request.command.dst;
    pending.origin = request.origin;
    pending.originRequestId = request.requestId;
    pending.isSigned = request.command.isSigned;
    pending.frame = plan.frame;
    pending.binary = plan.binary;
    pending.args = plan.args;
    pending.name = plan.name;

    uint32_t nowMs = clockMs();
    if (request.origin == Origin::MQTT) {
        // The same request again while it waits: it is on its way, so it is not sent twice.
        const PendingCommand* waiting = pending_.findRepeat(request.origin, pending.dst,
                                                            pending.originRequestId, pending.frame);
        if (waiting != nullptr) {
            acknowledge(*waiting, formatQueuedAck(*waiting, nowMs));
            return;
        }
    }
    CommandRoute route;
    if (!commandRoute(pending.dst, mesh.routeStateOf(pending.dst), route)) {
        reply(request, CommandStatus::NO_ROUTE);
        return;
    }

    // Read before the frame joins the queue, which the wait for the slot counts.
    pending.nextHop = route.nextHop;
    pending.timing = currentTiming(
        route, MessageManager::getInstance().commandAllowanceMs(pending.name.c_str()));
    pending.expectedMs = expectedIn(pending.dst, pending.nextHop, pending.timing);
    pending.requestId = pending_.nextRequestId(nowMs);
    CommandStatus sent = sendFrame(pending.dst, pending.requestId, pending.frame);
    if (sent != CommandStatus::OK) {
        reply(request, sent);
        return;
    }
    PendingCommand evicted;
    if (pending_.add(pending, nowMs, evicted)) deliver(evicted, CommandStatus::DROPPED);
    acknowledge(pending, formatQueuedAck(pending, nowMs));
    if (request.origin == Origin::SERIAL_CONSOLE) {
        reply(request, String("Sent to ") + String(pending.dst, HEX) + " [" + pending.requestId +
                           "], reply expected in " + ceilSeconds(pending.expectedMs) +
                           " s, given up after " + ceilSeconds(pending.giveUpMs) + " s");
    }
}

void CommandRouter::cancel(const CommandRequest& request) {
    PendingCommand pending;
    if (!pending_.cancel(request.origin, request.command.dst, request.requestId, clockMs(),
                         pending)) {
        ESP_LOGI(CR_TAG, "Nothing to cancel for %04X, request %u", request.command.dst,
                 request.requestId);
        return;
    }
    ESP_LOGI(CR_TAG, "Command to %04X [%u] cancelled", pending.dst, pending.requestId);
    deliver(pending, CommandStatus::CANCELLED);
}

void CommandRouter::deliver(const PendingCommand& pending, const CommandReply& result) {
    if (pending.origin == Origin::MQTT) {
        MqttService::getInstance().publishCommandReply(
            pending.dst, formatReplyPayload(pending.originRequestId, result.result()).c_str());
    } else {
        String text = MessageManager::getInstance().describeReply(pending.name.c_str(), result);
        Serial.printf("< %04X [%u] %s\n", pending.dst, pending.requestId, text.c_str());
    }
}

void CommandRouter::acknowledge(const PendingCommand& pending, const std::string& line) {
    if (pending.origin != Origin::MQTT) return;
    MqttService::getInstance().publishCommandAck(pending.dst, line.c_str());
}

void CommandRouter::sweepPending() {
    if (pending_.size() == 0) return;
    std::vector<PendingCommand> due;
    std::vector<PendingCommand> expired;
    uint32_t nowMs = clockMs();
    pending_.sweep(nowMs, due, expired);
    for (const PendingCommand& pending : due) retransmit(pending);
    for (const PendingCommand& pending : expired) {
        constexpr uint32_t HALF_SECOND_MS = 500;
        uint32_t waitedS = (pending.waitedMs(nowMs) + HALF_SECOND_MS) / 1000;
        deliver(pending, CommandReply(CommandStatus::TIMEOUT, String(waitedS)));
    }
}

void CommandRouter::retransmit(const PendingCommand& pending) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    uint32_t nowMs = clockMs();
    CommandStatus sent = CommandStatus::NO_ROUTE;
    CommandRoute route;
    AttemptTiming timing;
    if (commandRoute(pending.dst, mesh.routeStateOf(pending.dst), route)) {
        timing = currentTiming(route, pending.timing.allowanceMs);
        sent = sendFrame(pending.dst, pending.requestId, pending.frame);
    }
    PendingCommand updated;
    if (sent != CommandStatus::OK) {
        ESP_LOGW(CR_TAG, "No reply from %04X [%u] and the command cannot be sent again",
                 pending.dst, pending.requestId);
        if (pending_.giveUp(pending.dst, pending.requestId, nowMs, updated)) {
            deliver(updated, sent);
        }
        return;
    }
    ESP_LOGI(CR_TAG, "No reply from %04X [%u] yet, the command was sent again", pending.dst,
             pending.requestId);
    if (pending_.secondAttempt(pending.dst, pending.requestId, nowMs, timing,
                               expectedIn(pending.dst, route.nextHop, timing), updated)) {
        acknowledge(updated, formatRetransmitAck(updated, nowMs));
    }
}

void CommandRouter::reply(const CommandRequest& request, const CommandReply& result) {
    std::string name;
    std::string args;
    splitCommand(request.command.line, name, args);
    if (request.origin == Origin::SERIAL_CONSOLE) {
        String text = MessageManager::getInstance().describeReply(name.c_str(), result);
        Serial.printf("> %s\n%s\n", name.c_str(), text.c_str());
        return;
    }
    if (request.origin == Origin::MQTT) {
        // The server pairs a reply with the node the command was for, also when the gateway
        // answers in its place.
        MqttService::getInstance().publishCommandReply(
            request.command.dst, formatReplyPayload(request.requestId, result.result()).c_str());
        return;
    }
    if (request.origin == Origin::LORA) {
        CommandFrame frame;
        frame.response = true;
        frame.status = result.status;
        frame.line = result.text.c_str();
        size_t maxText =
            LoRaMeshService::getInstance().maxAppPayload() - CommandFrame::REPLY_HEADER_SIZE;
        if (frame.line.size() > maxText) {
            frame.line.resize(maxText - 1);
            frame.line += TRUNCATED_MARK;
        }
        if (sendFrame(request.src, request.requestId, frame.encode()) != CommandStatus::OK) {
            ESP_LOGW(CR_TAG, "The reply to %04X [%u] could not be sent", request.src,
                     request.requestId);
        }
    }
}

CommandStatus CommandRouter::sendFrame(uint16_t dst, uint8_t requestId,
                                       const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> buffer(sizeof(DataMessage) + bytes.size());
    DataMessage* message = reinterpret_cast<DataMessage*>(buffer.data());
    message->appPortDst = CommandApp;
    message->appPortSrc = CommandApp;
    message->messageId = requestId;
    message->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    message->addrDst = dst;
    message->messageSize = bytes.size();
    std::copy(bytes.begin(), bytes.end(), message->message);
    switch (LoRaMeshService::getInstance().send(message)) {
        case MeshSend::SENT:
            return CommandStatus::OK;
        case MeshSend::QUEUE_FULL:
            return CommandStatus::DROPPED;
        case MeshSend::NO_ROUTE:
        case MeshSend::FAILED:
            break;
    }
    return CommandStatus::NO_ROUTE;
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
