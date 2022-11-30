#include "wifiServerService.h"

void WiFiServerService::initWiFi() {
    createServerTask();

    bool hasValues = restartWiFiData();

    if (hasValues)
        connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        Log.verboseln("WiFi connected: %s", WiFi.localIP().toString().c_str());
        initWiFiServer();
    }

    Log.verboseln(F("WiFi initialized"));
}

void WiFiServerService::processReceivedMessage(messagePort port, DataMessage* message) {
}

String WiFiServerService::addSSID(String ssid) {
    this->ssid = ssid;

    return F("SSID added");
}

String WiFiServerService::addPassword(String password) {
    this->password = password;

    return F("Password added");
}

String WiFiServerService::saveWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    configService.setConfig("WiFiSSid", ssid);
    configService.setConfig("WiFiPsw", password);

    return F("WiFi data saved");
}

String WiFiServerService::connectWiFi() {
    WiFi.scanNetworks();

    WiFi.begin(ssid.c_str(), password.c_str());
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i < MAX_CONNECTION_TRY) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Log.verbose(F("."));
        i++;
    }

    return WiFi.status() == WL_CONNECTED ? F("WiFi connected") : F("WiFi connection failed");
}

String WiFiServerService::initWiFiServer() {
    server->begin();

    return startServer();
}

String WiFiServerService::startServer() {
    if (serverAvailable)
        return F("Server already available");

    server->begin();
    serverAvailable = true;

    xTaskNotifyGive(server_TaskHandle);

    return F("Server Started");
}

String WiFiServerService::stopServer() {
    if (!serverAvailable)
        return F("Server already unavailable");

    server->close();
    serverAvailable = false;

    return F("Server Stopped");
}

String WiFiServerService::getIP() {
    return WiFi.localIP().toString();
}

void WiFiServerService::ServerLoop(void*) {
    WiFiServerService& WiFiServerService = WiFiServerService::getInstance();
    String header = "";

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (WiFiServerService.serverAvailable) {

            WiFiClient client = WiFiServerService.server->available();   // Listen for incoming clients

            if (client) {                             // If a new client connects,
                Log.verboseln("New Client.");          // print a message out in the serial port
                String currentLine = "";                // make a String to hold incoming data from the client
                while (client.connected()) {  // loop while the client's connected
                    if (client.available()) {             // if there's bytes to read from the client,
                        char c = client.read();             // read a byte, then
                        Serial.write(c);                    // print it out the serial monitor
                        header += c;
                        if (c == '\n') {                    // if the byte is a newline character
                            // if the current line is blank, you got two newline characters in a row.
                            // that's the end of the client HTTP request, so send a response:
                            if (currentLine.length() == 0) {
                                // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                                // and a content-type so the client knows what's coming, then a blank line:
                                client.println(F("HTTP/1.1 200 OK"));
                                client.println(F("Content-type:text/html"));
                                client.println(F("Connection: close"));
                                client.println();

                                // turns the GPIOs on and off
                                if (header.indexOf("GET /blinkLed") >= 0) {
                                    Log.verboseln(F("Blink LED"));
                                    Helper::ledBlink(2, 100);
                                }

                                // Display the HTML web page
                                client.println(F("<!DOCTYPE html><html>"));
                                client.println(F("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
                                client.println(F("<link rel=\"icon\" href=\"data:,\">"));
                                // CSS to style the on/off buttons 
                                // Feel free to change the background-color and font-size attributes to fit your preferences
                                client.println(F("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}"));
                                client.println(F(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;"));
                                client.println(F("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}"));
                                client.println(F(".button2 {background-color: #555555;}</style></head>"));

                                // Web Page Heading
                                client.println(F("<body><h1>ESP32 Web Server</h1>"));

                                client.println(F("<p>Blink Led </p>"));
                                // If the output26State is off, it displays the ON button       
                                // if (output26State == "off")) {
                                client.println(F("<p><a href=\"/blinkLed\"><button class=\"button\">Blink Led</button></a></p>"));
                                // }
                                // else {
                                //     client.println("<p><a href=\"/26/off\"><button class=\"button button2\">OFF</button></a></p>"));
                                // }

                                // Display current state, and ON/OFF buttons for GPIO 27  
                                // client.println("<p>GPIO 27 - State " + output27State + "</p>"));
                                // // If the output27State is off, it displays the ON button       
                                // if (output27State == "off")) {
                                //     client.println("<p><a href=\"/27/on\"><button class=\"button\">ON</button></a></p>"));
                                // }
                                // else {
                                //     client.println("<p><a href=\"/27/off\"><button class=\"button button2\">OFF</button></a></p>"));
                                // }
                                client.println(F("</body></html>"));

                                // The HTTP response ends with another blank line
                                client.println();
                                // Break out of the while loop
                                break;
                            }
                            else { // if you got a newline, then clear currentLine
                                currentLine = "";
                            }
                        }
                        else if (c != '\r') {  // if you got anything else but a carriage return character,
                            currentLine += c;      // add it to the end of the currentLine
                        }
                    }
                }
                // Clear the header variable
                header = "";
                // Close the connection
                client.stop();
                Serial.println(F("Client disconnected."));
                Serial.println("");
            }

            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}

void WiFiServerService::createServerTask() {
    int res = xTaskCreate(
        ServerLoop, /* Function to implement the task */
        "Server Task", /* Name of the task */
        4096, /* Stack size in words */
        NULL, /* Task input parameter */
        1, /* Priority of the task */
        &server_TaskHandle); /* Task handle. */

    if (res != pdPASS)
        Log.errorln(F("Server task handle error: %d"), res);
}

bool WiFiServerService::restartWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    ssid = configService.getConfig("WiFiSSid", "DEFAULT_SSID");
    password = configService.getConfig("WiFiPsw", "DEFAULT_PASSWORD");

    return ssid != "DEFAULT_SSID" && password != "DEFAULT_PASSWORD";
}
