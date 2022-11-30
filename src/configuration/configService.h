#pragma once

#include <Arduino.h>

#include <Preferences.h>

class ConfigService {
public:

    static ConfigService& getInstance() {
        static ConfigService instance;
        return instance;
    }

    void setConfig(String key, String value);

    String getConfig(String key, String defaultValue);

private:
    ConfigService();

    Preferences preferences;
};