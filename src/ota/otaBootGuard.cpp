#include "otaBootGuard.h"

#include "bootloader_common.h"
#include "esp_flash_partitions.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc_wdt.h"

#include "loramesh/loraMeshService.h"
#include "otaKeys.h"

static const char* BG_TAG = "OtaBootGuard";

static const char* NVS_NAMESPACE = "lmota";
static const char* NVS_KEY_RECORD = "rec";
static const char* NVS_KEY_NEIGHBOURS = "nbr";
static const char* NVS_KEY_BOOTS = "boots";

static constexpr uint32_t BOOTLOADER_REGION_SIZE = 0x7000;

void OtaBootGuard::begin() {
    esp_err_t nvsErr = nvs_flash_init();
    bool nvsOk = nvsErr == ESP_OK && loadRecord();
    hadNeighbours_ = nvsOk && loadNeighbourFlag();
    if (nvsOk) countBoot();

    ImageState state = ImageState::UNDEFINED;
    esp_ota_img_states_t idfState;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &idfState) == ESP_OK) {
        switch (idfState) {
            case ESP_OTA_IMG_NEW:
                state = ImageState::NEW;
                break;
            case ESP_OTA_IMG_PENDING_VERIFY:
                state = ImageState::PENDING_VERIFY;
                break;
            case ESP_OTA_IMG_VALID:
                state = ImageState::VALID;
                break;
            case ESP_OTA_IMG_INVALID:
                state = ImageState::INVALID;
                break;
            case ESP_OTA_IMG_ABORTED:
                state = ImageState::ABORTED;
                break;
            default:
                state = ImageState::UNDEFINED;
                break;
        }
    }

    printBootLine(state);

    ShaPrefix runningSha{};
    if (record_.pending && state != ImageState::PENDING_VERIFY) runningSha = runningShaPrefix();

    BootDecision decision = BootGuardLogic::onBoot(state, runningSha, record_);
    if (decision.recordChanged) saveRecord(decision.record);
    reportDecision(decision.action);

    if (decision.action == BootAction::RUN_SELF_TEST) {
        startSelfTest();
        checkOtaPath(nvsOk);
    } else if (!hadNeighbours_) {
        xTaskCreate(
            [](void*) {
                OtaBootGuard::getInstance().runContactTask();
                vTaskDelete(NULL);
            },
            "OtaContact", 3072, nullptr, 1, nullptr);
    }
}

void OtaBootGuard::reportCheck(SelfTestCheck check, bool ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!selfTest_) return;
#if defined(TEST_IMAGE_KIND) && TEST_IMAGE_KIND == TEST_IMAGE_SELFTEST_FAIL
    if (check == SelfTestCheck::OTA_PATH) ok = false;
#endif
    selfTest_->report(check, ok);
}

void OtaBootGuard::reportSetupDone() {
#if defined(TEST_IMAGE_KIND) && TEST_IMAGE_KIND == TEST_IMAGE_LOOP_HANG
    if (isPendingVerify()) {
        ESP_LOGW(BG_TAG, "TEST_IMAGE loop-hang: spinning forever");
        for (;;) {
        }
    }
#endif
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t largest = ESP.getMaxAllocHeap();
    bool ok = BootGuardLogic::heapOk(freeHeap, largest);
    if (!ok) ESP_LOGE(BG_TAG, "Heap too low: free %u largest %u", freeHeap, largest);
    reportCheck(SelfTestCheck::HEAP, ok);
}

OtaRecord OtaBootGuard::record() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return record_;
}

void OtaBootGuard::recordAttempt(const ShaPrefix& sha) {
    saveRecord(BootGuardLogic::onAttemptStarted(record(), sha));
}

bool OtaBootGuard::loadRecord() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;

    size_t size = 0;
    err = nvs_get_blob(handle, NVS_KEY_RECORD, nullptr, &size);
    if (err == ESP_OK && size > 0) {
        std::vector<uint8_t> blob(size);
        err = nvs_get_blob(handle, NVS_KEY_RECORD, blob.data(), &size);
        if (err == ESP_OK && !OtaRecordCodec::decode(blob.data(), size, record_)) {
            ESP_LOGE(BG_TAG, "Stored OTA record is corrupt, ignoring it");
        }
    }
    nvs_close(handle);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

void OtaBootGuard::saveRecord(const OtaRecord& record) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        record_ = record;
    }
    std::vector<uint8_t> blob = OtaRecordCodec::encode(record);
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(BG_TAG, "Cannot open NVS to save the OTA record");
        return;
    }
    if (nvs_set_blob(handle, NVS_KEY_RECORD, blob.data(), blob.size()) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        ESP_LOGE(BG_TAG, "Cannot save the OTA record");
    }
    nvs_close(handle);
}

bool OtaBootGuard::loadNeighbourFlag() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t value = 0;
    nvs_get_u8(handle, NVS_KEY_NEIGHBOURS, &value);
    nvs_close(handle);
    return value != 0;
}

void OtaBootGuard::countBoot() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_get_u32(handle, NVS_KEY_BOOTS, &bootCount_);
    bootCount_++;
    if (nvs_set_u32(handle, NVS_KEY_BOOTS, bootCount_) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

void OtaBootGuard::saveNeighbourFlag() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, NVS_KEY_NEIGHBOURS, 1);
    nvs_commit(handle);
    nvs_close(handle);
    hadNeighbours_ = true;
    ESP_LOGI(BG_TAG, "Mesh contact recorded, future self-tests require a mesh frame");
}

void OtaBootGuard::startSelfTest() {
    uint32_t watchdogMs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        selfTestStartMs_ = millis();
        selfTest_.reset(new SelfTest(selfTestStartMs_, hadNeighbours_));
        watchdogMs = selfTest_->timing().watchdogMs;
    }
    armRtcWatchdog(watchdogMs);
    ESP_LOGW(BG_TAG, "Image in PENDING_VERIFY, self-test started (mesh frame %s)",
             hadNeighbours_ ? "required" : "not required");

    xTaskCreate(
        [](void*) {
            OtaBootGuard::getInstance().runSelfTestTask();
            vTaskDelete(NULL);
        },
        "OtaSelfTest", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr);

#if defined(TEST_IMAGE_KIND) && TEST_IMAGE_KIND == TEST_IMAGE_CRASH
    ESP_LOGW(BG_TAG, "TEST_IMAGE crash: aborting");
    abort();
#elif defined(TEST_IMAGE_KIND) && TEST_IMAGE_KIND == TEST_IMAGE_IRQ_HANG
    ESP_LOGW(BG_TAG, "TEST_IMAGE irq-hang: interrupts off forever");
    portDISABLE_INTERRUPTS();
    for (;;) {
    }
#endif
}

void OtaBootGuard::armRtcWatchdog(uint32_t timeoutMs) {
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, timeoutMs);
    rtc_wdt_enable();
    rtc_wdt_protect_on();
}

void OtaBootGuard::extendRtcWatchdog(uint32_t timeoutMs) {
    rtc_wdt_protect_off();
    rtc_wdt_set_time(RTC_WDT_STAGE0, timeoutMs);
    rtc_wdt_feed();
    rtc_wdt_protect_on();
}

void OtaBootGuard::runSelfTestTask() {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    for (;;) {
        bool contact = mesh.hasMeshContact();
        if (contact && !hadNeighbours_) saveNeighbourFlag();
        uint32_t superframeMs = mesh.getSuperframeDurationMs();

        SelfTestVerdict verdict;
        FailReason reason;
        bool extended;
        SelfTestTiming timing;
        uint32_t now = millis();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            extended = selfTest_->setSuperframe(superframeMs);
            timing = selfTest_->timing();
            if (contact) selfTest_->report(SelfTestCheck::MESH_FRAME, true);
            verdict = selfTest_->evaluate(now);
            reason = selfTest_->failReason();
        }
        if (extended) {
            extendRtcWatchdog(timing.watchdogMs - (now - selfTestStartMs_));
            ESP_LOGW(BG_TAG, "Superframe %u ms: mesh window %u s, deadline %u s", superframeMs,
                     timing.meshWindowMs / 1000, timing.deadlineMs / 1000);
        }
        if (verdict != SelfTestVerdict::WAITING) {
            finishSelfTest(verdict, reason);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void OtaBootGuard::runContactTask() {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    while (!mesh.hasMeshContact()) {
        vTaskDelay(pdMS_TO_TICKS(CONTACT_POLL_PERIOD_MS));
    }
    saveNeighbourFlag();
}

void OtaBootGuard::finishSelfTest(SelfTestVerdict verdict, FailReason reason) {
    if (verdict == SelfTestVerdict::PASS) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        rtc_wdt_disable();
        saveRecord(BootGuardLogic::onSelfTestPassed(record()));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            selfTest_.reset();
        }
        attemptResolved_ = true;
        ESP_LOGW(BG_TAG, "Self-test passed, image marked valid (%s)", esp_err_to_name(err));
        printBootLine(ImageState::VALID);
        return;
    }

    ESP_LOGE(BG_TAG, "Self-test failed, reason %u, rolling back", static_cast<unsigned>(reason));
    saveRecord(BootGuardLogic::onSelfTestFailed(record(), reason));
    esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(BG_TAG, "Rollback refused, restarting");
    esp_restart();
}

void OtaBootGuard::checkOtaPath(bool nvsOk) {
    bool nextSlot = esp_ota_get_next_update_partition(nullptr) != nullptr;
    int64_t start = esp_timer_get_time();
    bool signatureOk = otaSignaturePathWorks();
    int64_t verifyUs = esp_timer_get_time() - start;
    if (!nvsOk) ESP_LOGE(BG_TAG, "OTA path: NVS not readable");
    if (!nextSlot) ESP_LOGE(BG_TAG, "OTA path: no next update partition");
    if (!signatureOk) ESP_LOGE(BG_TAG, "OTA path: embedded manifest signature does not verify");
    ESP_LOGI(BG_TAG, "OTA path: signature check %s in %lld ms", signatureOk ? "ok" : "failed",
             verifyUs / 1000);
    reportCheck(SelfTestCheck::OTA_PATH, nvsOk && nextSlot && signatureOk);
}

void OtaBootGuard::printBootLine(ImageState state) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t* app = esp_ota_get_app_description();

    std::string bootloader = bootloaderHash();

    const char* reset = resetReasonName();

    std::string line = formatBootLine(running ? running->label : "?", state, app->version,
                                      bootloader.c_str(), reset);
    Serial.println(line.c_str());

    const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
    if (invalid) Serial.printf("BOOT last_invalid=%s\n", invalid->label);
}

void OtaBootGuard::reportDecision(BootAction action) {
    OtaRecord current = record();
    if (action == BootAction::REPORT_VALID || action == BootAction::REPORT_ROLLBACK ||
        action == BootAction::REPORT_BL_NO_ROLLBACK) {
        attemptResolved_ = true;
    }
    switch (action) {
        case BootAction::REPORT_VALID:
            ESP_LOGW(BG_TAG, "Update attempt resolved: VALID");
            break;
        case BootAction::REPORT_ROLLBACK:
            ESP_LOGE(BG_TAG, "Update attempt rolled back, reason %u, unexplained %u%s",
                     static_cast<unsigned>(current.failReason), current.unexplainedRollbacks,
                     current.blacklist.contains(current.attemptSha) ? ", blacklisted" : "");
            break;
        case BootAction::REPORT_BL_NO_ROLLBACK:
            ESP_LOGE(BG_TAG, "BL_NO_ROLLBACK: the bootloader on this board cannot roll back");
            break;
        default:
            break;
    }
}

const char* OtaBootGuard::resetReasonName() {
    static const char* RESET_REASONS[] = {"UNKNOWN", "POWERON", "EXT",      "SW",
                                          "PANIC",   "INT_WDT", "TASK_WDT", "WDT",
                                          "DEEPSLEEP", "BROWNOUT", "SDIO"};
    size_t rr = static_cast<size_t>(esp_reset_reason());
    return rr < sizeof(RESET_REASONS) / sizeof(RESET_REASONS[0]) ? RESET_REASONS[rr] : "UNKNOWN";
}

std::string OtaBootGuard::bootloaderHash() {
    char hash[7] = "??????";
    uint8_t sha[32];
    if (bootloader_common_get_sha256_of_partition(ESP_BOOTLOADER_OFFSET, BOOTLOADER_REGION_SIZE,
                                                  PART_TYPE_APP, sha) == ESP_OK) {
        snprintf(hash, sizeof(hash), "%02x%02x%02x", sha[0], sha[1], sha[2]);
    }
    return hash;
}

ShaPrefix OtaBootGuard::runningShaPrefix() {
    ShaPrefix prefix{};
    uint8_t sha[32];
    if (esp_partition_get_sha256(esp_ota_get_running_partition(), sha) == ESP_OK) {
        std::copy(sha, sha + prefix.size(), prefix.begin());
    }
    return prefix;
}
