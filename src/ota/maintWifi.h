#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "maintWindowPlan.h"

/**
 * @brief WiFi for maintenance on sensor nodes.
 *
 * Owns WiFi while a maintenance window is open and switches it off again afterwards, releasing
 * the driver. One window at a time:
 * - boot window: 60 s after boot, joins the node's WiFi network and runs the update check;
 * - pull window (/maint.open <s>): the same for up to the given time;
 * - AP window (/maint.open <s> ap): opens the WPA2 access point "LM-<address>", whose password
 *   derives from the deployment key, and serves the upload page.
 *
 * Windows opened by command follow a MaintWindowPlan kept in NVS (namespace "lmmaint") on a
 * node clock that survives reboots: at most 8 per 24 h, a window of the same kind extends the
 * open one, and an open window is resumed after a reboot (once the boot guard has decided) in
 * place of the boot window. A resumed access point closes 2 min after the update result.
 *
 * The station uses the same credentials as the gateway WiFi: those stored in NVS, otherwise
 * WIFI_SSID / WIFI_PASSWORD. It never makes the node a mesh gateway and never writes the
 * credentials. Gateways keep their own WiFi: on them /maint.open <s> runs the update check
 * right away over that WiFi, and the access point is refused.
 */
class MaintenanceWifi {
public:
    using Work = std::function<void()>;

    static MaintenanceWifi& getInstance() {
        static MaintenanceWifi instance;
        return instance;
    }

    /** Sets the work run once the network is joined; a pull window closes when it returns. */
    void onConnected(Work work) { work_ = std::move(work); }

    /** Loads the window plan and the node clock from NVS. Call once, before startBootWindow(). */
    void begin();

    /**
     * @brief Resumes a window that was open before the reboot, or else opens the boot window if
     *        MaintWindow::decideBootWindow() allows it.
     * @param gateway the node is a gateway (it keeps its own WiFi).
     * @param pendingVerify the running image is still being verified.
     */
    void startBootWindow(bool gateway, bool pendingVerify);

    /** Opens or extends a pull window of @p durationS seconds. @return the reply. */
    std::string openPullWindow(uint32_t durationS) { return open(Kind::PULL, durationS); }

    /** Opens or extends the access point for @p durationS seconds. @return the reply. */
    std::string openApWindow(uint32_t durationS) { return open(Kind::AP, durationS); }

    /** Closes the open window. @return the reply to the command. */
    std::string closeWindow();

    /**
     * @brief Saves the station credentials in the WiFi driver's NVS config, where the windows
     *        (and a gateway) read them. Refused while a window is open.
     * @return the reply to the command.
     */
    std::string storeCredentials(const std::string& ssid, const std::string& password);

    /** Saves the node clock. Call before a planned reboot. */
    void saveClock();

private:
    enum class Kind : uint8_t { BOOT, PULL, AP };

    MaintenanceWifi() = default;

    std::string open(Kind kind, uint32_t durationS);
    /** Runs the update check now over the WiFi a gateway keeps connected. */
    std::string checkOverGatewayWifi();
    bool startTask(Kind kind);
    static void windowTask(void* parameter);
    static void resumeTask(void* parameter);
    static void clockTick(void* parameter);
    void runWindow();
    /** @return ms left in the running window, 0 once it is over. */
    uint32_t remainingMs();
    /** Waits until one of @p bits (or the close request) is set, or the window is over. */
    EventBits_t waitInWindow(EventBits_t bits);
    uint32_t nodeNow() const;
    void savePlan();
    void runStationWindow();
    void runApWindow();
    /** Starts the network stack and the WiFi driver with a netif of the window's kind. */
    bool initWifi(bool accessPoint);
    /** Loads the station credentials. @return false if there are none. */
    bool loadCredentials();
    bool startStation();
    bool startAccessPoint(const std::string& ssid, const std::string& password);
    bool registerEvents();
    void stopWifi();

    Work work_;
    /** Kind of the running window. */
    Kind kind_ = Kind::BOOT;
    int64_t bootWindowEndUs_ = 0;
    std::mutex planMutex_;
    MaintWindowPlan plan_;
    /** Node clock at boot (s), continued from NVS. */
    uint32_t clockAtBoot_ = 0;
    uint32_t clockSavedAt_ = 0;
    bool gateway_ = false;
    bool pendingVerify_ = false;
    char ssid_[33] = {};
    char password_[65] = {};
    esp_netif_t* netif_ = nullptr;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stopping_{false};
};
