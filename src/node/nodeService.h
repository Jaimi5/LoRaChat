#pragma once

#include <Arduino.h>

#include "message/messageService.h"
#include "nodeCommandService.h"
#include "nodeRole.h"
#include "sleepMode.h"

/**
 * @brief Node-level settings stored in NVS: the role (gateway or sensor) and the sleep settings.
 *
 * The role decides at boot whether WiFi and MQTT run and whether the node is the LoRaMesher
 * network manager. The sleep mode decides how a sensor's MCU sleeps in the SLEEP slots; the sleep
 * fraction is the minimum share of the superframe the network sleeps, set by the network manager.
 */
class NodeService : public MessageService {
public:
    static NodeService& getInstance() {
        static NodeService instance;
        return instance;
    }

    /**
     * Loads the role and the sleep settings from NVS, or picks the default role for @p address.
     * NVS must be ready.
     */
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

    SleepMode sleepMode() const { return sleepMode_; }

    /** @return the stored minimum sleep fraction in percent, or -1 if none is stored. */
    int sleepFractionPercent() const { return sleepFraction_; }

    /** @return the sleep mode and sleep fraction, each marked stored or default. */
    String describeSleep() const;

    /**
     * @brief Stores the sleep mode named in @p text and restarts the node to apply it.
     * @return the reply to the command.
     */
    String setSleepMode(const String& text);

    /**
     * @brief Stores the minimum sleep fraction (whole percent, 0 to 90) and restarts the node.
     * @return the reply to the command.
     */
    String setSleepFraction(const String& text);

private:
    NodeService() : MessageService(NodeApp, "Node") { commandService = &commands_; }

    NodeCommandService commands_;
    /** Stores @p value under @p key and restarts the node. @return the reply, or an error. */
    String storeAndRestart(const char* key, uint8_t value, const String& done);

    NodeRole role_ = NodeRole::SENSOR;
    bool stored_ = false;
    SleepMode sleepMode_ = SleepMode::NONE;
    bool sleepModeStored_ = false;
    int sleepFraction_ = -1;
};
