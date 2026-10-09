#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "otaManifest.h"

/** 32-byte secret shared by the nodes of one deployment and the operator's tools. */
using DeploymentKey = std::array<uint8_t, 32>;

/**
 * @brief Parses 64 hex digits (surrounding spaces allowed) into @p out.
 * @return false if the text is not a key, or the key is all zeros.
 */
bool parseDeploymentKey(const std::string& text, DeploymentKey& out);

/** @return the first 4 bytes of SHA-256(@p key) in hex, to compare keys without showing them. */
std::string keyFingerprint(const DeploymentKey& key);

/** @return the report key Kr = HMAC-SHA256(key, "LMR1"); the server only stores Kr. */
Sha256 reportKey(const DeploymentKey& key);

/** @return the X-LM-Tag header of a report: hex(HMAC-SHA256(Kr, body)), 64 characters. */
std::string reportTag(const Sha256& reportKey, const std::string& body);

/** @return HMAC-SHA256 of @p data under @p key. */
Sha256 hmacSha256(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size);

/** @return the name of the node's maintenance AP, "LM-" and the address in 4 hex digits. */
std::string apSsid(uint16_t nodeAddress);

/**
 * @return the WPA2 password of the node's maintenance AP:
 *         hex(HMAC-SHA256(key, "LMAP1" || address u16 LE)[:8]), 16 lowercase characters.
 *         scripts/ota_tools/ap_pass.py computes the same.
 */
std::string apPassword(const DeploymentKey& key, uint16_t nodeAddress);
