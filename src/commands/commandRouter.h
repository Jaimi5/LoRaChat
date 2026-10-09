#pragma once

#include <Arduino.h>

#include <array>

#include "cmdAuth.h"
#include "cmdFrame.h"
#include "cmdPerm.h"
#include "cmdText.h"
#include "commands/commandService.h"
#include "message/messageService.h"

/** A command line waiting to run, and where its reply goes. */
struct CommandRequest {
    Origin origin = Origin::SERIAL_CONSOLE;
    /** Sender on LoRa. */
    uint16_t src = 0;
    /** Request id of the sender (the LoRaChat messageId on LoRa). */
    uint8_t requestId = 0;
    /** The line is the reply of node src to a command forwarded from here. */
    bool response = false;
    CommandLine command;
};

/**
 * @brief Runs text commands from every channel, one at a time, in its own task.
 *
 * Lines from the serial console, MQTT text on cmd/<gateway> and command frames from LoRa
 * (CommandApp) are queued here.
 * Signed lines are checked against the deployment key and the replay counter (NVS lmsec/ctr)
 * before they run; the permission of each command decides what a channel may run. A command
 * for another node (serial "@<dst> ...", or an MQTT dst that is not this gateway) is sent over
 * LoRa; its reply is printed on serial or published on cmd-resp/<dst> when it arrives.
 */
class CommandRouter : public MessageService {
public:
    static CommandRouter& getInstance() {
        static CommandRouter instance;
        return instance;
    }

    /** Creates the queue and the task. */
    void begin();

    /** Queues @p request. Safe from any task. @return false if the queue is full. */
    bool submit(const CommandRequest& request);

    /** Command frames from LoRa: requests to run here, or replies to forwarded commands. */
    void processReceivedMessage(messagePort port, DataMessage* message) override;

    /**
     * @brief The counter of the signed command that is running, for commands that decrypt
     *        their arguments. Only valid inside a command handler.
     * @return false if the running command is not signed.
     */
    bool runningCounter(uint32_t& out) const;

private:
    static constexpr size_t PENDING_SLOTS = 8;

    /** A forwarded command whose reply has not arrived yet. */
    struct Pending {
        bool used = false;
        uint16_t dst = 0;
        uint8_t requestId = 0;
        /** Channel and request id the command came from, where its reply goes. */
        Origin origin = Origin::SERIAL_CONSOLE;
        uint8_t originRequestId = 0;
    };

    CommandRouter() : MessageService(CommandApp, "Command") { commandService = &commands_; }

    static void task(void* parameter);
    void handle(const CommandRequest& request);
    String runLocal(const CommandRequest& request);
    String runSigned(const CommandRequest& request);
    void forward(const CommandRequest& request);
    void reply(const CommandRequest& request, const String& text);
    void sendFrame(uint16_t dst, uint8_t requestId, const CommandFrame& frame);
    bool loadCounter();
    bool storeCounter(uint32_t counter);

    CommandService commands_;
    QueueHandle_t queue_ = nullptr;
    CommandCounter counter_;
    bool counterLoaded_ = false;
    uint8_t nextRequestId_ = 0;
    std::array<Pending, PENDING_SLOTS> pending_{};
    size_t nextPending_ = 0;
    const CommandRequest* running_ = nullptr;
};
