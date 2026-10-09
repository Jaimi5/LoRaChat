#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cmdAuth.h"

/**
 * @brief Encrypts or decrypts the arguments of a command (XOR with a keystream).
 *
 * Keystream block i is HMAC-SHA256(Kw, dst u16 LE || counter u32 LE || i u8), where
 * @p sealHmac is keyed with Kw = HMAC-SHA256(deployment key, "LMW1"). The counter of a signed
 * command never repeats for a node, so the keystream is never reused, and the command tag
 * covers the ciphertext.
 */
std::vector<uint8_t> sealCommandArgs(const KeyedHmac& sealHmac, uint16_t dst, uint32_t counter,
                                     const std::vector<uint8_t>& data);

/** @return "ssid\0password", the plaintext of /maint.wifi. */
std::vector<uint8_t> encodeWifiCredentials(const std::string& ssid, const std::string& password);

/**
 * @brief Splits a /maint.wifi plaintext.
 * @return false unless the SSID has 1-32 bytes and the password is empty (open network) or has
 *         8-63 printable characters (WPA2).
 */
bool decodeWifiCredentials(const std::vector<uint8_t>& data, std::string& ssid,
                           std::string& password);
