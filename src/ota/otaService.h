#pragma once

#include <Arduino.h>

#include "deploymentKey.h"
#include "message/messageService.h"
#include "otaCommandService.h"

/**
 * @brief OTA settings and maintenance windows on request.
 *
 * Keeps the deployment key in NVS (namespace "lmsec"). The key derives the password of the
 * node's maintenance access point and, later, signs commands and reports.
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

private:
    OtaService() : MessageService(OTAApp, "Ota") { commandService = &commands_; }

    OtaCommandService commands_;
};
