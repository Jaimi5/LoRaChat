#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>

#include "esp_netif.h"

/**
 * @brief WiFi for maintenance on sensor nodes.
 *
 * Joins the node's WiFi network for a limited window, runs the maintenance work (the OTA check)
 * and switches WiFi off again, releasing the driver. It uses the same credentials as the
 * gateway WiFi: those stored in NVS, otherwise WIFI_SSID / WIFI_PASSWORD. It is independent of
 * WiFiServerService: it never makes the node a mesh gateway and never writes the credentials.
 */
class MaintenanceWifi {
public:
    using Work = std::function<void()>;

    static MaintenanceWifi& getInstance() {
        static MaintenanceWifi instance;
        return instance;
    }

    /** Sets the work run once the hotspot is joined; the window closes when it returns. */
    void onConnected(Work work) { work_ = std::move(work); }

    /**
     * @brief Opens the boot window if MaintWindow::decideBootWindow() allows it.
     * @param gateway the node is a gateway (it keeps its own WiFi).
     * @param pendingVerify the running image is still being verified.
     */
    void startBootWindow(bool gateway, bool pendingVerify);

private:
    MaintenanceWifi() = default;

    static void windowTask(void* parameter);
    void runWindow(uint32_t durationMs);
    /** Initialises WiFi and loads the credentials. @return false if there are none. */
    bool loadCredentials();
    bool startWifi();
    void stopWifi();

    Work work_;
    bool gateway_ = false;
    bool pendingVerify_ = false;
    char ssid_[33] = {};
    char password_[65] = {};
    esp_netif_t* netif_ = nullptr;
    std::atomic<bool> stopping_{false};
};
