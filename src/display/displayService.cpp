#include "displayService.h"
#include "images.h"

void DisplayService::createDisplayTask() {
    xTaskCreatePinnedToCore(
        displayTask, /* Task function. */
        "DisplayTask", /* name of task. */
        2048, /* Stack size of task */
        this, /* parameter of the task */
        3, /* priority of the task */
        &display_TaskHandle, /* Task handle to keep track of created task */
        0); /* pin task to core 0 */
}

void DisplayService::init() {
    ESP_LOGV(DISPLAY_TAG, "Initializing Display Service");

    // Initialize the wire
    Wire1.begin(DISPLAY_SDA, DISPLAY_SCL);

    if (DISPLAY_RST != -1) {
        pinMode(DISPLAY_RST, OUTPUT);
        digitalWrite(DISPLAY_RST, LOW);
        delay(20);
        digitalWrite(DISPLAY_RST, HIGH);
    }

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, false, false)) {
        ESP_LOGE(DISPLAY_TAG, "SSD1306 allocation failed");
        return;
    }

    ESP_LOGV(DISPLAY_TAG, "Display allocation Done");

    display.clearDisplay();

    display.setTextColor(WHITE); // Draw white text

    display.setTextWrap(false);

    delay(50);

    displayTextVector.resize(maxLines, "");
    xPos.resize(maxLines, 0);
    minXPos.resize(maxLines, 0);
    moveStatus.resize(maxLines, false);

    // Set the title
    setTitle();

    // Create display task
    createDisplayTask();

    initialized = true;
}

String DisplayService::displayOn(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayOn, dst);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display On";
    }

    DisplayService& displayService = DisplayService::getInstance();
    displayService.displayOnFlag = true;

    return "Display On";
}

String DisplayService::displayOff(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayOff, dst);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display Off";
    }

    DisplayService& displayService = DisplayService::getInstance();
    displayService.displayOnFlag = false;

    displayService.display.clearDisplay();
    displayService.display.display();

    return "Display Off";
}


String DisplayService::displayBlink(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayBlink, dst);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display Blink";
    }

    DisplayService& displayService = DisplayService::getInstance();

    displayService.displayOnFlag = false;
    displayService.display.clearDisplay();

    for (int i = 0; i < 5; i++) {
        // Display a white screen
        displayService.display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, WHITE);
        displayService.display.display();

        vTaskDelay(500 / portTICK_PERIOD_MS);
        // Display a black screen
        displayService.display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BLACK);
        displayService.display.display();

        if (i == 4) break;
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    displayService.displayOnFlag = true;

    return "Display Blink";
}

String DisplayService::clearDisplay(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayClear, dst);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display Clear";
    }

    DisplayService& displayService = DisplayService::getInstance();
    displayService.display.clearDisplay();
    displayService.display.display();

    // Clear the text vector, but keep the title and device ID
    displayService.displayTextVector.resize(staticLines, "");
    displayService.xPos.resize(staticLines, 0);
    displayService.minXPos.resize(staticLines, 0);
    displayService.moveStatus.resize(staticLines, false);

    return "Display Clear";
}

void DisplayService::displayTask(void* pvParameters) {
    DisplayService& displayService = DisplayService::getInstance();
    ESP_LOGV(DISPLAY_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));

    // Display the initial logo
    displayService.displayLogo(0);

    for (;;) {
        if (displayService.displayingLogo) {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            displayService.displayingLogo = false;
        }

        if (displayService.displayOnFlag) {
            displayService.drawDisplay();
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

String DisplayService::displayLogo(uint16_t dst, uint16_t src) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayLogo, dst);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display Logo";
    }

    DisplayService& displayService = DisplayService::getInstance();
    Adafruit_SSD1306& display = displayService.display;
    displayService.displayingLogo = true;
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);
    String title = "LoRaChat";
    if (src != 0) {
        title += " - From " + String(src, HEX);
    }
    // Print the title in the middle of the screen
    display.setCursor(DISPLAY_WIDTH / 2 - title.length() / 2 * 6, 0);
    display.println(title);

    // displayTest.drawXBitmap(0, 0, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
    // Draw a bitmap icon in the middle of the screen
    display.drawXBitmap((DISPLAY_WIDTH - LOGO_WIDTH) / 2, 15, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, WHITE);

    display.display();

    return "Display Logo";
}

String DisplayService::displayText(uint16_t dst, String text, uint16_t src) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoraMesher::getInstance().getLocalAddress()) {
        DataMessage* msg = getDisplayMessage(DisplayCommand::DisplayText, dst, text);
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

        delete msg;

        return "Send Display Text";
    }

    if (src != 0) {
        text = "From " + String(src, HEX) + ": " + text;
    }

    DisplayService& displayService = DisplayService::getInstance();
    displayService.addText(text);

    return "Display Text Done";
}

String DisplayService::getJSON(DataMessage* message) {
    return "";
}

DataMessage* DisplayService::getDataMessage(JsonObject data) {
    DisplayMessage* displayMessage = new DisplayMessage();

    displayMessage->deserialize(data);

    return ((DataMessage*) displayMessage);
}

DataMessage* DisplayService::getDisplayMessage(DisplayCommand command, uint16_t dst, String text) {
    DisplayMessage* displayMessage = new DisplayMessage();

    displayMessage->displayCommand = command;

    size_t displayTextSize = 0;

    switch (command) {
        case DisplayCommand::DisplayText: {
                displayTextSize = text.length();
                if (displayTextSize > 128) {
                    ESP_LOGE(DISPLAY_TAG, "displayText is too long");
                    return nullptr;
                }
                strcpy(displayMessage->displayText, text.c_str());
                break;
            }
        default:
            break;
    }

    displayMessage->messageSize = displayTextSize + sizeof(DisplayCommand);

    displayMessage->appPortSrc = appPort::DisplayApp;
    displayMessage->appPortDst = appPort::DisplayApp;

    displayMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    displayMessage->addrDst = dst;

    return ((DataMessage*) displayMessage);
}

void DisplayService::processReceivedMessage(messagePort port, DataMessage* message) {
    DisplayService& displayService = DisplayService::getInstance();
    DisplayMessage* displayMessage = (DisplayMessage*) message;

    switch (displayMessage->displayCommand) {
        case DisplayCommand::DisplayOn:
            displayService.displayOn(0);
            break;
        case DisplayCommand::DisplayOff:
            displayService.displayOff(0);
            break;
        case DisplayCommand::DisplayBlink:
            displayService.displayBlink(0);
            break;
        case DisplayCommand::DisplayClear:
            displayService.clearDisplay(0);
            break;
        case DisplayCommand::DisplayText:
            displayService.displayText(0, displayMessage->displayText);
            break;
        case DisplayCommand::DisplayLogo:
            displayService.displayLogo(0);
            break;
        default:
            break;
    }
}

void DisplayService::printGPSData(String data) {
    if (!initialized) {
        ESP_LOGW(DISPLAY_TAG, "Display Service not initialized");
        return;
    }

    if (data.isEmpty() || data.length() == 0) {
        ESP_LOGE(DISPLAY_TAG, "GPS data is empty");
        return;
    }

    // Remove the leading and trailing brackets
    displayTextVector[2] = data;
    setupTextMovement(2, data);
}

void DisplayService::drawDisplay() {
    display.clearDisplay();
    for (int i = 0; i < displayTextVector.size(); ++i) {
        printLine(displayTextVector[i], xPos[i], i * lineHeight, 1, minXPos[i], moveStatus[i]);
    }
    display.display();
}

void DisplayService::printLine(const String& str, int& x, int y, int size, int minX, bool move) {
    display.setTextSize(size);
    display.setCursor(x, y);
    display.print(str);

    if (move) {
        x -= 2;
        if (x < minX) x = display.width();
    }
}

void DisplayService::addText(String text) {
    if (!initialized) {
        ESP_LOGW(DISPLAY_TAG, "Display Service not initialized");
        return;
    }


    // Only update the lines below the first two static lines
    for (int i = displayTextVector.size() - 1; i > staticLines; --i) {
        displayTextVector[i] = displayTextVector[i - 1];
        setupTextMovement(i, displayTextVector[i]);
    }

    if (staticLines < displayTextVector.size()) { // Check to prevent access to non-existent vector elements
        displayTextVector[staticLines] = text; // Add new text to the first dynamic line
        setupTextMovement(staticLines, text); // Set up movement only if necessary
    }

    if (displayTextVector.size() > maxLines) {
        displayTextVector.erase(displayTextVector.begin() + maxLines, displayTextVector.end());
        xPos.erase(xPos.begin() + maxLines, xPos.end());
        minXPos.erase(minXPos.begin() + maxLines, minXPos.end());
        moveStatus.erase(moveStatus.begin() + maxLines, moveStatus.end());
    }
}

void DisplayService::setupTextMovement(int line, const String& text) {
    if (text.length() > 20) {
        xPos[line] = display.width();
        minXPos[line] = -6 * text.length();
        moveStatus[line] = true;
    }
    else {
        xPos[line] = 0;
        minXPos[line] = 0;
        moveStatus[line] = false;
    }
}

void DisplayService::setTitle() {
    String title = "LoRaMesher - v0.0.8";
    String device_id = "Device ID: " + String(LoRaMeshService::getInstance().getLocalAddress(), HEX);

    // Method to update the title
    displayTextVector[0] = title;
    setupTextMovement(0, title); // Ensure title settings are correct, but typically no scroll

    // Method to update the device ID
    displayTextVector[1] = device_id;
    setupTextMovement(1, device_id); // Ensure device ID settings are correct, but typically no scroll
}
