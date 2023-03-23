#pragma once

#include <Arduino.h>

#include <WiFi.h>

#include <WiFiMulti.h>

#include <HTTPClient.h>

#include <ArduinoJson.h>

#include "config.h"

class HTTPService {
public:
    HTTPService();
    static bool sendHTTPPOST(String content);
};