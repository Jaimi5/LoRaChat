#include "configService.h"

void ConfigService::setConfig(String key, String value) {
    preferences.putString(key.c_str(), value);
}

String ConfigService::getConfig(String key, String defaultValue) {
    return preferences.getString(key.c_str(), defaultValue);
}