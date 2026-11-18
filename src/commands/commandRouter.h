#pragma once

#include <Arduino.h>

#include <vector>

#include "cmdAuth.h"
#include "cmdFrame.h"
#include "cmdPending.h"
#include "cmdPerm.h"
#include "cmdRoute.h"
#include "cmdRtt.h"
#include "cmdStatus.h"
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
    /** Status of a reply. */
    uint8_t status = 0;
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
 * A forwarded command always gets an answer, a status of CommandStatus. NO_ROUTE comes at once
 * when the routing table has no route to the node (commandRoute()), and nothing is sent then.
 * The gateway waits for the reply as long as a lossless one can
 * take (attemptTimeMs()) and never sends a command again before that. A signed command is then
 * sent once more. TIMEOUT follows when the wait ends, and a command pushed out of the full
 * table of waiting commands gets DROPPED. A reply that arrives up to ten minutes after its
 * TIMEOUT is still delivered. Commands from MQTT also get progress lines on cmd-ack/<dst>.
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

    /** @return the channel of the command that is running. Only valid inside a handler. */
    Origin runningOrigin() const;

private:
    static constexpr size_t PENDING_SLOTS = 8;

    CommandRouter();

    static void task(void* parameter);
    void handle(const CommandRequest& request);
    /** Pairs the reply of another node with the command it answers and delivers it. */
    void handleReply(const CommandRequest& request);
    CommandReply runLocal(const CommandRequest& request);
    CommandReply runSigned(const CommandRequest& request);
    void forward(const CommandRequest& request);
    void reply(const CommandRequest& request, const CommandReply& result);
    /** Hands the answer to a forwarded command to the channel the command came from. */
    void deliver(const PendingCommand& pending, const CommandReply& result);
    /** Publishes a progress line of a command that came from MQTT. */
    void acknowledge(const PendingCommand& pending, const std::string& line);
    /** Sends again or times out the forwarded commands that are due. */
    void sweepPending();
    /** Sends the due command once more, or gives it up if the mesh refuses it. */
    void retransmit(const PendingCommand& pending);
    /** @return what the wait for a transmission over @p route depends on, read from the mesh. */
    AttemptTiming currentTiming(const CommandRoute& route, uint32_t allowanceMs);
    /** @return the time until the reply to a transmission with @p timing is expected. */
    uint32_t expectedIn(uint16_t dst, uint16_t nextHop, const AttemptTiming& timing);
    /** @return the status of the failure, or OK if the mesh took the frame. */
    CommandStatus sendFrame(uint16_t dst, uint8_t requestId, const std::vector<uint8_t>& bytes);
    bool loadCounter();
    bool storeCounter(uint32_t counter);

    CommandService commands_;
    OwnedQueue<CommandRequest> queue_;
    CommandCounter counter_;
    bool counterLoaded_ = false;
    /** Forwarded commands that wait for a reply. Only the command task uses it. */
    PendingCommands pending_{PENDING_SLOTS};
    /** Measured round trip times, for the expected time of a reply. */
    RttEstimator rtt_;
    const CommandRequest* running_ = nullptr;
};
