#include "httpService.h"

HTTPService::HTTPService() {
}

bool HTTPService::sendHTTPPOST(String content) {
    HTTPClient http;

    http.begin(String(SERVER_URL));
    http.addHeader("Content-Type", "application/json", true, true);
    http.addHeader("Accept", "*/*", false, true);

    int res = http.POST(content);
    Serial.println(content);

    if (res == -1) {
        Serial.println("Server did not respond");
        return false;
    }
    else {
        Serial.print("Content sent to server. Response code: ");
        Serial.println(res);
        Serial.println(http.getString());
        return true;
    }
    http.end();
}