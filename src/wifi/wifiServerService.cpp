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

void WiFiServerService::sendAdditionalBodyHTML(WiFiClient client) {
    //Set input field html with button to send the command
    //add header
    client.println(F("<h2>Send command</h2>"));
    client.println(F("<form action=\"/command\" method=\"get\">"));
    client.println(F("<input type=\"text\" name=\"command\" value=\"\">"));
    client.println(F("<input type=\"submit\" value=\"Send\">"));
    client.println(F("</form>"));

    //Get the commands from MessageManager and send them to the client
    MessageManager& messageManager = MessageManager::getInstance();
    client.println("<h2>Available Commands</h2>");
    client.println("<div class=\"commands\">");
    client.println(messageManager.getAvailableCommandsHTML());
    client.println("</div>");
}

void WiFiServerService::responseCommand(WiFiClient client, String header) {
    String commandResponse = "";

    Log.verboseln(F("Command"));
    int commandStart = header.indexOf("command=");
    int commandEnd = header.indexOf(" HTTP/1.1");
    String command = header.substring(commandStart + 8, commandEnd);
    command.replace("+", " ");
    Log.verboseln(command.c_str());
    MessageManager& messageManager = MessageManager::getInstance();
    // messageManager.sendMessage(messagePort::WIFI, command);
    commandResponse = messageManager.executeCommand("/" + command);

    //Send command response html with a title
    if (!commandResponse.isEmpty()) {
        client.println(F("<h2>Command response</h2>"));
        client.println(commandResponse);
    }
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



                                client.printf(indexHeaderHTML);
                                client.printf(indexBodyHTML);

                                // turns the GPIOs on and off
                                if (header.indexOf("GET /blinkLed") >= 0) {
                                    Log.verboseln(F("Blink LED"));
                                    Helper::ledBlink(2, 100);
                                }
                                else if (header.indexOf("GET /command") >= 0) {
                                    WiFiServerService.responseCommand(client, header);
                                }

                                WiFiServerService.sendAdditionalBodyHTML(client);
                                client.printf(indexFooterHTML);

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
