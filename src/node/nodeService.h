#pragma once

#include <Arduino.h>

#include "message/messageService.h"
#include "nodeCommandService.h"
#include "nodeRole.h"

/**
 * @brief Node-level settings: the role of the node (gateway or sensor), stored in NVS.
 *
 * The role decides at boot whether WiFi and MQTT run and whether the node is the LoRaMesher
 * network manager.
 */
class NodeService : public MessageService {
public:
    static NodeService& getInstance() {
        static NodeService instance;
        return instance;
    }

    /** Loads the role from NVS, or picks the default role for @p address. NVS must be ready. */
    void init(uint16_t address);

    NodeRole role() const { return role_; }

    bool isGateway() const { return role_ == NodeRole::GATEWAY; }

    /** @return the role and whether it is stored or the default. */
    String describeRole() const;

    /**
     * @brief Stores the role named in @p text and restarts the node to apply it.
     * @return the reply to the command.
     */
    String setRole(const String& text);

private:
    NodeService() : MessageService(NodeApp, "Node") { commandService = &commands_; }

    NodeCommandService commands_;
    NodeRole role_ = NodeRole::SENSOR;
    bool stored_ = false;
};
