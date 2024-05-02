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
}

String DisplayService::displayOn() {
    DisplayService& displayService = DisplayService::getInstance();
    displayService.displayOnFlag = true;

    return "Display On";
}

String DisplayService::displayOff() {
    DisplayService& displayService = DisplayService::getInstance();
    displayService.displayOnFlag = false;

    displayService.display.clearDisplay();
    displayService.display.display();

    return "Display Off";
}


String DisplayService::displayBlink() {
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

String DisplayService::clearDisplay() {
    DisplayService& displayService = DisplayService::getInstance();
    displayService.display.clearDisplay();
    displayService.display.display();

    // Clear the text vector, but keep the title and device ID
    displayService.displayTextVector.resize(2, "");
    displayService.xPos.resize(2, 0);
    displayService.minXPos.resize(2, 0);
    displayService.moveStatus.resize(2, false);

    return "Display Clear";
}

void DisplayService::displayTask(void* pvParameters) {
    DisplayService& displayService = DisplayService::getInstance();
    ESP_LOGV(DISPLAY_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));

    // Display the initial logo
    displayService.displayLogo();

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

String DisplayService::displayLogo() {
    DisplayService& displayService = DisplayService::getInstance();
    Adafruit_SSD1306& display = displayService.display;
    displayService.displayingLogo = true;
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);
    const char* title = "LoRaChat"; // Fix: Change char* to const char*
    // Print the title in the middle of the screen
    display.setCursor(DISPLAY_WIDTH / 2 - strlen(title) / 2 * 6, 0);
    display.println(title);

    // displayTest.drawXBitmap(0, 0, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
    // Draw a bitmap icon in the middle of the screen
    display.drawXBitmap((DISPLAY_WIDTH - LOGO_WIDTH) / 2, 15, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, WHITE);

    display.display();

    return "Display Logo";
}

String DisplayService::displayText(String text) {
    DisplayService& displayService = DisplayService::getInstance();
    displayService.addText(text);

    return "Display Text Done";
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
