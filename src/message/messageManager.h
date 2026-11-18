#pragma once

#include <Arduino.h>

#include <vector>

#include "dataMessage.h"

#include "messageService.h"

#include "loramesh/loraMeshService.h"

#include "mqtt/mqttService.h"

#include "wifi/wifiServerService.h"

class MessageManager {
public:
    /**
     * @brief Construct a new MessageManager object
     *
     */
    static MessageManager& getInstance() {
        static MessageManager instance;
        return instance;
    }

    void init();

    void addMessageService(MessageService* service);

    void processReceivedMessage(messagePort port, DataMessage* message);

    void sendMessage(messagePort port, DataMessage* message);

    /**
     * @brief Runs one command line (name and arguments, without target or signature).
     * @param origin channel the line arrived on, which decides which commands may run.
     * @param signedValid the line carried a valid signature.
     * @return the reply with its status: UNKNOWN, PERM, or what the command returns.
     */
    CommandReply executeCommand(const String& line, Origin origin, bool signedValid);

    /** @return the commands that may run from @p origin, one per line. */
    String help(Origin origin) const;

    /** @return the page of /cmds that @p args asks for, or ARG for a page that does not exist. */
    CommandReply catalogue(const String& args) const;

    /** @return the time command @p name may take to run in ms, 0 if it declares none. */
    uint32_t commandAllowanceMs(const String& name) const;

    /**
     * @return @p reply as a person reads it: the text of a success, the usage line of command
     *         @p name for a bad argument, and the text of any other error.
     */
    String describeReply(const String& name, const CommandReply& reply) const;

    String getJSON(DataMessage* message);

    DataMessagePtr getDataMessage(const String& json);

    /** Logs the header fields of @p message. */
    void logHeader(const char* title, const DataMessage* message);

private:
    MessageManager(){};

    std::vector<MessageService*> services;

    /** Every command of every service, each name once. Filled while the services register. */
    CommandCatalogue catalogue_;

    static void sendMessageLoRaMesher(DataMessage* message);

    static void sendMessageMqtt(DataMessage* message);
};