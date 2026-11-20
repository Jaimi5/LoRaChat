#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

#include "deploymentKey.h"
#include "message/messageService.h"
#include "otaCommandService.h"

/** Error of /key.set, /ota.server and /reboot: the command could not do its work. */
constexpr uint8_t OTA_ERR_FAILED = 128;

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

    /**
     * @brief Writes the reply fields of /key: the marker of a node without a key, or the four
     *        bytes of the fingerprint of the stored key.
     */
    TypedResult keyInto(Fields& out) const;

    /** Stores the key given as 64 hex digits. @return the reply to the command. */
    CommandReply setKey(const String& text);

    /**
     * @brief Opens a maintenance window for @p seconds, as a pull window or as the access
     *        point.
     * @param out gets the reply fields of /maint.open (MaintOpenField in maintWifi.h).
     * @return success, ARG for a time that is 0 or above the limit of the kind, or one of
     *         MaintOpenError.
     */
    TypedResult openWindow(uint32_t seconds, bool accessPoint, Fields& out);

    /**
     * @brief Closes the open window. Field 0 of @p out is the marker that none was open,
     *        field 1 the marker that one is closing.
     */
    TypedResult closeWindow(Fields& out);

    /**
     * @brief Writes the reply fields of /version: the three numbers of the version (0.0.0 if
     *        the image version does not start with three numbers), the slot (0 app0, 1 app1,
     *        absent for another partition), the image state (0 VALID, 1 PENDING_VERIFY), the
     *        first three bytes of the bootloader hash (zeros if it cannot be read), the board
     *        env (0 tbeam, 1 tbeam-debug, absent for another env), the first four bytes of the
     *        commit the image was built from (zeros if the version names none) and the marker
     *        of a build with uncommitted changes.
     */
    TypedResult versionInto(Fields& out) const;

    /**
     * @brief Writes the reply fields of /ota.status: the outcome of the last update attempt,
     *        whether an attempt is pending (0 no, 1 yes), the fail reason, the unexplained
     *        rollbacks and the number of blacklisted images.
     */
    TypedResult statusInto(Fields& out) const;

    /** Restarts the node once the reply has left. @return success or OTA_ERR_FAILED. */
    TypedResult reboot();

    /**
     * @brief Shows or sets the OTA server: no argument shows it, "default" goes back to
     *        OTA_SERVER_URL, an http:// URL is stored in NVS. @return the reply.
     */
    CommandReply setServer(const String& args);

    /** @return the OTA server folder: the one stored with /ota.server, else OTA_SERVER_URL. */
    std::string serverUrl() const;

    /**
     * @brief Stores the WiFi credentials of the node (gateway WiFi and maintenance windows)
     *        from the bytes of a signed command, which carries them encrypted for this node
     *        and its counter (scripts/ota_tools/lmcmd.py wifi).
     * @param out gets the reply fields of /maint.wifi (MaintWifiField in maintWifi.h).
     * @return success, ARG without a signed command or a deployment key, or one of
     *         MaintWifiError.
     */
    TypedResult setWifiSealed(const std::vector<uint8_t>& sealed, Fields& out);

    /** Stores the WiFi credentials typed as "<ssid> [<password>]", as setWifiSealed() does. */
    TypedResult setWifiPlain(const String& args, Fields& out);

private:
    TypedResult storeWifi(const std::string& ssid, const std::string& password, Fields& out);

    OtaService() : MessageService(OTAApp, "Ota") { commandService = &commands_; }

    OtaCommandService commands_;
};
