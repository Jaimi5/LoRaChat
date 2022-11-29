#include "configService.h"

ConfigService::ConfigService() {
    preferences.begin("config", false);
}

void ConfigService::setConfig(String key, String value) {
    preferences.putString(key.c_str(), value);
}

String ConfigService::getConfig(String key, String defaultValue) {
    return preferences.getString(key.c_str(), defaultValue);
}