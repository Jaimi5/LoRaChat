#include "otaInstall.h"

#include <Arduino.h>

#include <algorithm>

#include "bootloader_common.h"
#include "esp_attr.h"
#include "esp_flash_partitions.h"
#include "esp_ota_ops.h"

#include "config.h"
#include "devices/initDevices.h"
#include "loramesh/loraMeshService.h"
#include "maintWifi.h"
#include "otaBootGuard.h"
#include "otaKeys.h"
#include "otaSignature.h"

static const char* OI_TAG = "OtaInstall";
static constexpr uint32_t LOG_FLUSH_MS = 100;

namespace {

/** Image whose download or check failed. Survives esp_restart(), not a power cut. */
struct FailedImage {
    uint32_t magic;
    ShaPrefix sha;
};

constexpr uint32_t FAILED_IMAGE_MAGIC = 0x4C4D4631;
RTC_NOINIT_ATTR FailedImage failedImage;

bool loadFailedImage(ShaPrefix& out) {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) failedImage.magic = 0;
    if (failedImage.magic != FAILED_IMAGE_MAGIC) return false;
    out = failedImage.sha;
    return true;
}

}  // namespace

NodeState OtaInstall::nodeState() {
    NodeState state;
    state.boardEnv = BUILD_ENV_NAME;
    const char* running = esp_ota_get_app_description()->version;
    if (!OtaPolicy::parseVersion(running, state.runningVersion)) {
        ESP_LOGW(OI_TAG, "Running version %s cannot be parsed", running);
    }

    uint8_t sha[32];
    if (bootloader_common_get_sha256_of_partition(ESP_PARTITION_TABLE_OFFSET,
                                                  ESP_PARTITION_TABLE_MAX_LEN, PART_TYPE_DATA,
                                                  sha) == ESP_OK) {
        std::copy(sha, sha + state.partitionTablePrefix.size(), state.partitionTablePrefix.begin());
    }
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    state.slotSize = next != nullptr ? next->size : 0;
    state.blacklist = OtaBootGuard::getInstance().record().blacklist;
    state.hasFailedImage = loadFailedImage(state.failedImage);

    PowerState power;
    if (InitDevices::readPower(power)) {
        state.batteryMv = power.batteryMv;
        state.externalPower = power.externalPower;
    }
    state.freeHeap = esp_get_free_heap_size();
    return state;
}

bool OtaInstall::accept(const uint8_t* data, size_t size, OtaManifest& out,
                        std::string& reason) {
    ManifestCheck check = verifySignedManifest(data, size, otaTrustedKeys(), otaAcceptTestKeys(),
                                               verifyP256Sha256, out);
    if (check != ManifestCheck::OK) {
        reason = manifestCheckName(check);
        ESP_LOGE(OI_TAG, "Manifest rejected: %s", reason.c_str());
        return false;
    }
    ESP_LOGI(OI_TAG, "Manifest for %s %s, %u B, key %u", out.boardEnv.c_str(),
             out.versionString.c_str(), out.imageSize, out.keyId);

    PolicyDecision decision = OtaPolicy::decide(out, nodeState());
    reason = policyDecisionName(decision);
    if (decision != PolicyDecision::INSTALL) {
        ESP_LOGI(OI_TAG, "Not installing it: %s", reason.c_str());
        return false;
    }
    return true;
}

void OtaInstall::stopMesh(const OtaManifest& manifest) {
    ESP_LOGW(OI_TAG, "Installing %s, stopping the mesh", manifest.versionString.c_str());
    LoRaMeshService::getInstance().standby();
}

void OtaInstall::rebootInto(EspOtaFlash& flash, const OtaManifest& manifest) {
    OtaBootGuard::getInstance().recordAttempt(manifest.imageShaPrefix());
    if (!flash.activate()) restartAfterFailure(manifest);
    ESP_LOGW(OI_TAG, "Rebooting into %s on %s", manifest.versionString.c_str(),
             flash.partition()->label);
    MaintenanceWifi::getInstance().saveClock();
    vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
    esp_restart();
}

void OtaInstall::restartAfterFailure(const OtaManifest& manifest) {
    failedImage.magic = FAILED_IMAGE_MAGIC;
    failedImage.sha = manifest.imageShaPrefix();
    ESP_LOGE(OI_TAG, "Update failed, restarting the running image");
    MaintenanceWifi::getInstance().saveClock();
    vTaskDelay(pdMS_TO_TICKS(LOG_FLUSH_MS));
    esp_restart();
}
