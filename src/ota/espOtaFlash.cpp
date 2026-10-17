#include "espOtaFlash.h"

#include <cstring>

#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_timer.h"

static const char* EF_TAG = "EspOtaFlash";

static bool check(esp_err_t err, const char* what) {
    if (err != ESP_OK) ESP_LOGE(EF_TAG, "%s failed: %s", what, esp_err_to_name(err));
    return err == ESP_OK;
}

bool EspOtaFlash::begin(uint32_t size) {
    if (partition_ == nullptr || handle_ != 0 || size > partition_->size) return false;
    int64_t start = esp_timer_get_time();
    esp_err_t err = esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
    busyUs_ += esp_timer_get_time() - start;
    offset_ = 0;
    pending_.clear();
    return check(err, "esp_ota_begin");
}

bool EspOtaFlash::write(const uint8_t* data, size_t size) {
    if (handle_ == 0) return false;
    pending_.insert(pending_.end(), data, data + size);
    size_t now = pending_.size();
    if ((offset_ + now) % SPI_FLASH_SEC_SIZE == 0) now--;
    if (now == 0) return true;
    bool ok = writeToFlash(pending_.data(), now);
    pending_.erase(pending_.begin(), pending_.begin() + now);
    return ok;
}

bool EspOtaFlash::writeToFlash(const uint8_t* data, size_t size) {
    int64_t start = esp_timer_get_time();
    esp_err_t err = esp_ota_write(handle_, data, size);
    busyUs_ += esp_timer_get_time() - start;
    offset_ += size;
    return check(err, "esp_ota_write");
}

bool EspOtaFlash::end() {
    if (handle_ == 0) return false;
    if (!pending_.empty() && !writeToFlash(pending_.data(), pending_.size())) {
        abort();
        return false;
    }
    pending_.clear();
    esp_ota_handle_t handle = handle_;
    handle_ = 0;
    int64_t start = esp_timer_get_time();
    esp_err_t err = esp_ota_end(handle);
    busyUs_ += esp_timer_get_time() - start;
    return check(err, "esp_ota_end");
}

void EspOtaFlash::abort() {
    pending_.clear();
    if (handle_ == 0) return;
    esp_ota_abort(handle_);
    handle_ = 0;
}

bool EspOtaFlash::writtenSha(Sha256& out) {
    return partition_ != nullptr &&
           check(esp_partition_get_sha256(partition_, out.data()), "esp_partition_get_sha256");
}

bool EspOtaFlash::readDescriptor(ImageDescriptor& out) {
    esp_app_desc_t desc;
    if (partition_ == nullptr ||
        !check(esp_ota_get_partition_description(partition_, &desc), "Reading the descriptor")) {
        return false;
    }
    out.project.assign(desc.project_name, strnlen(desc.project_name, sizeof(desc.project_name)));
    out.version.assign(desc.version, strnlen(desc.version, sizeof(desc.version)));
    return true;
}

bool EspOtaFlash::activate() {
    return partition_ != nullptr &&
           check(esp_ota_set_boot_partition(partition_), "esp_ota_set_boot_partition");
}
