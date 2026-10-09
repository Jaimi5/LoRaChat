#pragma once

#include <string>

#include "deploymentKey.h"

/**
 * @return the report key Kr = HMAC-SHA256(K, "LMR1") of the deployment key K. The server
 *         holds only Kr, so it can check reports but cannot sign commands.
 */
Sha256 reportKey(const DeploymentKey& key);

/** @return hex(HMAC-SHA256(Kr, body)), sent as the X-LM-Tag header of POST /report. */
std::string reportTag(const Sha256& reportKey, const std::string& body);
