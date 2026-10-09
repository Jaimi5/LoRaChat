#include "cmdSeal.h"

#include <algorithm>

static constexpr size_t MAX_SSID = 32;
static constexpr size_t MIN_PASSWORD = 8;
static constexpr size_t MAX_PASSWORD = 63;

std::vector<uint8_t> sealCommandArgs(const KeyedHmac& sealHmac, uint16_t dst, uint32_t counter,
                                     const std::vector<uint8_t>& data) {
    uint8_t blockInput[7] = {static_cast<uint8_t>(dst & 0xFF), static_cast<uint8_t>(dst >> 8)};
    for (int i = 0; i < 4; i++) blockInput[2 + i] = static_cast<uint8_t>(counter >> (8 * i));

    std::vector<uint8_t> out(data);
    HmacDigest stream{};
    for (size_t i = 0; i < out.size(); i++) {
        if (i % stream.size() == 0) {
            blockInput[6] = static_cast<uint8_t>(i / stream.size());
            stream = sealHmac(blockInput, sizeof(blockInput));
        }
        out[i] ^= stream[i % stream.size()];
    }
    return out;
}

std::vector<uint8_t> encodeWifiCredentials(const std::string& ssid, const std::string& password) {
    std::vector<uint8_t> data(ssid.begin(), ssid.end());
    data.push_back(0);
    data.insert(data.end(), password.begin(), password.end());
    return data;
}

bool decodeWifiCredentials(const std::vector<uint8_t>& data, std::string& ssid,
                           std::string& password) {
    auto separator = std::find(data.begin(), data.end(), 0);
    if (separator == data.end()) return false;
    std::string name(data.begin(), separator);
    std::string secret(separator + 1, data.end());
    bool nameOk = !name.empty() && name.size() <= MAX_SSID &&
                  std::none_of(name.begin(), name.end(),
                               [](char c) { return static_cast<uint8_t>(c) < 0x20; });
    bool secretOk = secret.empty() ||
                    (secret.size() >= MIN_PASSWORD && secret.size() <= MAX_PASSWORD &&
                     std::all_of(secret.begin(), secret.end(), [](char c) {
                         return static_cast<uint8_t>(c) >= 0x20 && static_cast<uint8_t>(c) < 0x7F;
                     }));
    if (!nameOk || !secretOk) return false;
    ssid = name;
    password = secret;
    return true;
}
