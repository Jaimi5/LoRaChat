#include "deferredRestart.h"

#include <Arduino.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "loramesh/loraMeshService.h"
#include "ota/maintWifi.h"

static const char* DR_TAG = "DeferredRestart";
/** Time between two looks at the mesh TX queue. */
static constexpr uint64_t POLL_US = 250ULL * 1000;

bool DeferredRestart::request() {
    if (pending_.exchange(true)) return true;
    requestedUs_ = esp_timer_get_time();
    gate_ = RestartGate();

    esp_timer_create_args_t args = {};
    args.callback = poll;
    args.arg = this;
    args.name = "restart";
    args.skip_unhandled_events = true;
    esp_timer_handle_t timer = nullptr;
    esp_err_t err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, POLL_US);
        if (err != ESP_OK) esp_timer_delete(timer);
    }
    if (err != ESP_OK) {
        ESP_LOGE(DR_TAG, "The restart timer could not be started: %s", esp_err_to_name(err));
        pending_ = false;
        return false;
    }
    return true;
}

void DeferredRestart::poll(void* parameter) {
    auto* self = static_cast<DeferredRestart*>(parameter);
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    auto elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - self->requestedUs_) / 1000);
    size_t queued = mesh.GetTxQueueSize();
    if (!self->gate_.due(elapsedMs, queued, mesh.getSuperframeDurationMs())) return;

    ESP_LOGW(DR_TAG, "Restarting %u ms after the request, %u messages in the TX queue",
             static_cast<unsigned>(elapsedMs), static_cast<unsigned>(queued));
    MaintenanceWifi::getInstance().saveClock();
    esp_restart();
}
