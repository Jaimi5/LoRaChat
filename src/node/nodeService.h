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
/** Errors of /role.set, /sleep.set and /sleep.fraction. */
constexpr uint8_t NODE_ERR_VERIFYING = 128;
constexpr uint8_t NODE_ERR_STORE = 129;
constexpr uint8_t NODE_ERR_RESTART = 130;
/** Error of /identify: the task that blinks could not be started. */
constexpr uint8_t NODE_ERR_IDENTIFY = 128;

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

    /** Writes the reply fields of /role: the role value, then 0 default or 1 stored. */
    TypedResult roleInto(Fields& out) const;

    /**
     * @brief Stores the role with value @p role and restarts the node to apply it.
     * @return success, ARG for a value that is no role, or an error of the setting commands.
     */
    TypedResult setRole(uint32_t role);

    SleepMode sleepMode() const { return sleepMode_; }

    /** @return the stored minimum sleep fraction in percent, or -1 if none is stored. */
    int sleepFractionPercent() const { return sleepFraction_; }

    /** @return the sleep mode and sleep fraction, each marked stored or default. */
    String describeSleep() const;

    /**
     * @brief Writes the first four reply fields of /sleep: the mode value, 0 default or 1
     *        stored, then the stored fraction in percent or, without one, the marker of the
     *        default fraction.
     */
    void sleepInto(Fields& out) const;

    /**
     * @brief Stores the sleep mode with value @p mode and restarts the node to apply it.
     * @return success, ARG for a value that is no mode, or an error of the setting commands.
     */
    TypedResult setSleepMode(uint32_t mode);

    /**
     * @brief Stores the minimum sleep fraction (whole percent, 0 to 90) and restarts the node.
     * @return success, ARG above 90, or an error of the setting commands.
     */
    TypedResult setSleepFraction(uint32_t percent);

private:
    NodeService() : MessageService(NodeApp, "Node") { commandService = &commands_; }

    NodeCommandService commands_;
    /**
     * Stores @p value under @p key and restarts the node once the reply has left.
     * @return success, or an error with the esp_err_t code where there is one.
     */
    TypedResult storeAndRestart(const char* key, uint8_t value);

    NodeRole role_ = NodeRole::SENSOR;
    bool stored_ = false;
    SleepMode sleepMode_ = SleepMode::NONE;
    bool sleepModeStored_ = false;
    int sleepFraction_ = -1;
};
