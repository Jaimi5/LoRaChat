#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>
#include <string>

#include "esp_netif.h"

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
 * The station uses the same credentials as the gateway WiFi: those stored in NVS, otherwise
 * WIFI_SSID / WIFI_PASSWORD. It never makes the node a mesh gateway and never writes the
 * credentials. Gateways keep their own WiFi and do not open windows.
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

    /**
     * @brief Opens the boot window if MaintWindow::decideBootWindow() allows it.
     * @param gateway the node is a gateway (it keeps its own WiFi).
     * @param pendingVerify the running image is still being verified.
     */
    void startBootWindow(bool gateway, bool pendingVerify);

    /** Opens a pull window of up to @p durationMs. @return the reply to the command. */
    std::string openPullWindow(uint32_t durationMs);

    /** Opens the access point for @p durationMs. @return the reply to the command. */
    std::string openApWindow(uint32_t durationMs);

    /** Closes the open window. @return the reply to the command. */
    std::string closeWindow();

private:
    enum class Kind : uint8_t { BOOT, PULL, AP };

    MaintenanceWifi() = default;

    std::string open(Kind kind, uint32_t durationMs);
    static void windowTask(void* parameter);
    void runWindow();
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
    Kind kind_ = Kind::BOOT;
    uint32_t durationMs_ = 0;
    bool gateway_ = false;
    bool pendingVerify_ = false;
    char ssid_[33] = {};
    char password_[65] = {};
    esp_netif_t* netif_ = nullptr;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stopping_{false};
};
