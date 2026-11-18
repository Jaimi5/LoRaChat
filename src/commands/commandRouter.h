#pragma once

#include <Arduino.h>

#include <vector>

#include "cmdAuth.h"
#include "cmdFrame.h"
#include "cmdPending.h"
#include "cmdPerm.h"
#include "cmdText.h"
#include "commands/commandService.h"
#include "message/messageService.h"
#include "message/ownedQueue.h"

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
 *
 * A forwarded command always gets an answer. Without a route to the node it is "ERR no route".
 * A signed command is sent once more after half the timeout. When no reply arrives within
 * the timeout the answer is "ERR timeout <n> s", and a command pushed out of the full table
 * of waiting commands gets "ERR dropped".
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

    CommandRouter() : MessageService(CommandApp, "Command") { commandService = &commands_; }

    static void task(void* parameter);
    void handle(const CommandRequest& request);
    String runLocal(const CommandRequest& request);
    String runSigned(const CommandRequest& request);
    void forward(const CommandRequest& request);
    void reply(const CommandRequest& request, const String& text);
    /** Hands the answer to a forwarded command to the channel the command came from. */
    void deliver(const PendingCommand& pending, const String& text);
    /** Sends again or times out the forwarded commands that are due. */
    void sweepPending();
    void sendFrame(uint16_t dst, uint8_t requestId, const std::vector<uint8_t>& bytes);
    bool loadCounter();
    bool storeCounter(uint32_t counter);

    CommandService commands_;
    OwnedQueue<CommandRequest> queue_;
    CommandCounter counter_;
    bool counterLoaded_ = false;
    /** Forwarded commands that wait for a reply. Only the command task uses it. */
    PendingCommands pending_{PENDING_SLOTS};
    const CommandRequest* running_ = nullptr;
};
