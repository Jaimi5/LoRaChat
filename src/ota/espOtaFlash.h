#pragma once

#include <vector>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "iOtaFlash.h"

/**
 * @brief IOtaFlash on the next OTA app partition, with the esp_ota_* API.
 *
 * Writes sequentially (OTA_WITH_SEQUENTIAL_WRITES): each sector is erased when the write
 * reaches it, so there is no long full-slot erase before the first byte. In IDF 4.4 a write
 * that ends on a sector boundary also erases the following sector, which the next write erases
 * again; such writes are held back by one byte, so every sector is erased once.
 */
class EspOtaFlash : public IOtaFlash {
public:
    EspOtaFlash() : partition_(esp_ota_get_next_update_partition(nullptr)) {}
    ~EspOtaFlash() override { abort(); }
    EspOtaFlash(const EspOtaFlash&) = delete;
    EspOtaFlash& operator=(const EspOtaFlash&) = delete;

    bool begin(uint32_t size) override;
    bool write(const uint8_t* data, size_t size) override;
    bool end() override;
    void abort() override;
    bool writtenSha(Sha256& out) override;
    bool readDescriptor(ImageDescriptor& out) override;
    bool activate() override;

    /** @return the partition written to, or nullptr if there is none. */
    const esp_partition_t* partition() const { return partition_; }

    /** @return time spent in begin(), write() and end() (erasing and programming), in ms. */
    uint32_t busyMs() const { return static_cast<uint32_t>(busyUs_ / 1000); }

private:
    const esp_partition_t* partition_;
    esp_ota_handle_t handle_ = 0;
    int64_t busyUs_ = 0;
    /** Bytes passed to esp_ota_write() since begin(). */
    uint32_t offset_ = 0;
    /** Bytes received but not written yet. */
    std::vector<uint8_t> pending_;

    bool writeToFlash(const uint8_t* data, size_t size);
};
