#pragma once

#include <Arduino.h>

#include "deploymentKey.h"
#include "message/messageService.h"
#include "otaCommandService.h"

/**
 * @brief OTA settings and maintenance windows on request.
 *
 * Keeps the deployment key in NVS (namespace "lmsec"). The key derives the password of the
 * node's maintenance access point and the keys that sign commands and reports.
 */
class OtaService : public MessageService {
public:
    static OtaService& getInstance() {
        static OtaService instance;
        return instance;
    }

    /** @return true and the key in @p out if a deployment key is stored. */
    bool deploymentKey(DeploymentKey& out) const;

    /** @return whether a key is stored, with its fingerprint. */
    String describeKey() const;

    /** Stores the key given as 64 hex digits. @return the reply to the command. */
    String setKey(const String& text);

    /** Opens a window as "<seconds> [ap]". @return the reply to the command. */
    String openWindow(const String& args);

    /** @return the reply to the command. */
    String closeWindow();

    /** @return version, slot, image state, bootloader hash, board env and node address. */
    String version() const;

    /** @return the last update attempt and its outcome. */
    String otaStatus() const;

    /** Restarts the node after a short delay. @return the reply to the command. */
    String reboot();

    /**
     * @brief Shows or sets the OTA server: no argument shows it, "default" goes back to
     *        OTA_SERVER_URL, an http:// URL is stored in NVS. @return the reply.
     */
    String setServer(const String& args);

    /** @return the OTA server folder: the one stored with /ota.server, else OTA_SERVER_URL. */
    std::string serverUrl() const;

    /**
     * @brief Stores the WiFi credentials of the node (gateway WiFi and maintenance windows).
     *
     * A signed command carries them encrypted for this node and its counter
     * (scripts/ota_tools/lmcmd.py wifi); on the serial console they may be typed as
     * "<ssid> [<password>]". @return the reply.
     */
    String setWifi(const String& args);

private:
    OtaService() : MessageService(OTAApp, "Ota") { commandService = &commands_; }

    OtaCommandService commands_;
};
