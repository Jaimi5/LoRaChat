#include "displayService.h"
#include "images.h"

#include "displayText.h"

#include "esp_ota_ops.h"
#include "esp_timer.h"

static const char* DISPLAY_TAG = "DisplayService";
static constexpr TickType_t LOGO_HOLD_TICKS = 10000 / portTICK_PERIOD_MS;
static constexpr TickType_t BLINK_HALF_PERIOD_TICKS = 500 / portTICK_PERIOD_MS;
static constexpr int BLINK_COUNT = 5;
static constexpr size_t SCROLL_ABOVE_CHARS = 20;
static constexpr int CHAR_WIDTH = 6;

void DisplayService::createDisplayTask() {
    xTaskCreatePinnedToCore(displayTask,         /* Task function. */
                            "DisplayTask",       /* name of task. */
                            4096,                /* Stack size of task */
                            this,                /* parameter of the task */
                            3,                   /* priority of the task */
                            &display_TaskHandle, /* Task handle to keep track of created task */
                            0);                  /* pin task to core 0 */
}

void DisplayService::init() {
    ESP_LOGV(DISPLAY_TAG, "Initializing Display Service");

#if DISPLAY_RST >= 0
    pinMode(DISPLAY_RST, OUTPUT);
    digitalWrite(DISPLAY_RST, LOW);
    delay(20);
    digitalWrite(DISPLAY_RST, HIGH);
#endif

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, false, false)) {
        ESP_LOGE(DISPLAY_TAG, "SSD1306 allocation failed");
        return;
    }

    ESP_LOGV(DISPLAY_TAG, "Display allocation Done");

    display.clearDisplay();

    display.setTextColor(WHITE);  // Draw white text

    display.setTextWrap(false);

    delay(50);

    // Set the title
    setTitle();

    initialized = true;
    sleepAtUs = esp_timer_get_time() + static_cast<int64_t>(DISPLAY_AWAKE_MS) * 1000;

    // Create display task
    createDisplayTask();
}

void DisplayService::wake() {
    if (!initialized)
        return;
    sleepAtUs = esp_timer_get_time() + static_cast<int64_t>(DISPLAY_AWAKE_MS) * 1000;
    xTaskNotifyGive(display_TaskHandle);
}

void DisplayService::requestPanel(PanelRequest request) {
    panelRequests_.fetch_or(request);
    wake();
}

String DisplayService::displayOn(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayOn, dst);
        if (!msg) return "No memory for the message";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display On";
    }

    displayOnFlag = true;
    wake();

    return "Display On";
}

String DisplayService::displayOff(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayOff, dst);
        if (!msg) return "No memory for the message";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display Off";
    }

    sleepAtUs = 0;
    xTaskNotifyGive(display_TaskHandle);

    return "Display Off";
}


String DisplayService::displayBlink(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayBlink, dst);
        if (!msg) return "No memory for the message";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display Blink";
    }

    requestPanel(PANEL_BLINK);

    return "Display Blink";
}

String DisplayService::clearDisplay(uint16_t dst) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayClear, dst);
        if (!msg) return "No memory for the message";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display Clear";
    }

    std::lock_guard<std::mutex> lock(linesMutex_);
    lines_.clearLog();

    return "Display Clear";
}

void DisplayService::displayTask(void* pvParameters) {
    DisplayService& displayService = *static_cast<DisplayService*>(pvParameters);
    ESP_LOGV(DISPLAY_TAG, "Stack space unused after entering the task: %u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    displayService.drawLogo(0);

    for (;;) {
        if (esp_timer_get_time() >= displayService.sleepAtUs) {
            if (displayService.panelOn) {
                displayService.display.ssd1306_command(SSD1306_DISPLAYOFF);
                displayService.panelOn = false;
                ESP_LOGI(DISPLAY_TAG, "Display sleeping");
            }
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!displayService.panelOn) {
            displayService.display.ssd1306_command(SSD1306_DISPLAYON);
            displayService.panelOn = true;
            ESP_LOGI(DISPLAY_TAG, "Display awake");
        }

        uint32_t requests = displayService.panelRequests_.exchange(0);
        if (requests & PANEL_LOGO) displayService.drawLogo(displayService.logoSrc_);
        if (requests & PANEL_BLINK) displayService.blink();

        if (displayService.displayOnFlag) {
            displayService.drawDisplay();
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void DisplayService::blink() {
    for (int i = 0; i < BLINK_COUNT; i++) {
        display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, WHITE);
        display.display();
        vTaskDelay(BLINK_HALF_PERIOD_TICKS);

        display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, BLACK);
        display.display();
        if (i < BLINK_COUNT - 1) vTaskDelay(BLINK_HALF_PERIOD_TICKS);
    }
}

String DisplayService::displayLogo(uint16_t dst, uint16_t src) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayLogo, dst);
        if (!msg) return "No memory for the message";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display Logo";
    }

    logoSrc_ = src;
    requestPanel(PANEL_LOGO);

    return "Display Logo";
}

void DisplayService::drawLogo(uint16_t src) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);
    String title = "LoRaChat";
    if (src != 0) {
        title += " - From " + String(src, HEX);
    }
    // Print the title in the middle of the screen
    display.setCursor(DISPLAY_WIDTH / 2 - title.length() / 2 * CHAR_WIDTH, 0);
    display.println(title);

    // Draw a bitmap icon in the middle of the screen
    display.drawXBitmap((DISPLAY_WIDTH - LOGO_WIDTH) / 2, 15, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT,
                        WHITE);

    display.display();
    vTaskDelay(LOGO_HOLD_TICKS);
}

String DisplayService::displayText(uint16_t dst, String text, uint16_t src) {
    if (!initialized)
        return "Display Service not initialized";

    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        DataMessagePtr msg = getDisplayMessage(DisplayCommand::DisplayText, dst, text);
        if (!msg) return "Text longer than " + String(maxRemoteTextLength()) + " characters";
        MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg.get());

        return "Send Display Text";
    }

    if (src != 0) {
        text = "From " + String(src, HEX) + ": " + text;
    }

    addText(text);

    return "Display Text Done";
}

String DisplayService::getJSON(DataMessage* message) {
    return "";
}

DataMessagePtr DisplayService::getDataMessage(JsonObject data) {
    static constexpr size_t MAX_JSON_TEXT = 32;
    DataMessagePtr message =
        allocateDataMessage(sizeof(DisplayMessage) - sizeof(DataMessageGeneric));
    if (!message) return nullptr;
    auto* displayMessage = reinterpret_cast<DisplayMessage*>(message.get());

    message->deserialize(data);
    displayMessage->displayCommand = data["displayCommand"];

    size_t displayTextSize = 0;
    if (displayMessage->displayCommand == DisplayText) {
        const char* text = data["displayText"] | "";
        size_t length = strlen(text);
        if (length > MAX_JSON_TEXT) {
            ESP_LOGE(DISPLAY_TAG, "displayText is too long");
        } else {
            memcpy(displayMessage->displayText, text, length + 1);
            displayTextSize = length + 1;
        }
    }
    message->messageSize = displayTextSize + sizeof(DisplayCommand);

    return message;
}

size_t DisplayService::maxRemoteTextLength() {
    return displayTextLimit(LoRaMeshService::getInstance().maxAppPayload(),
                            sizeof(DisplayMessage::displayText));
}

DataMessagePtr DisplayService::getDisplayMessage(DisplayCommand command, uint16_t dst,
                                                 String text) {
    if (command == DisplayCommand::DisplayText && text.length() > maxRemoteTextLength()) {
        ESP_LOGE(DISPLAY_TAG, "displayText is too long");
        return nullptr;
    }

    DataMessagePtr message =
        allocateDataMessage(sizeof(DisplayMessage) - sizeof(DataMessageGeneric));
    if (!message) return nullptr;
    auto* displayMessage = reinterpret_cast<DisplayMessage*>(message.get());

    displayMessage->displayCommand = command;

    size_t displayTextSize = 0;
    if (command == DisplayCommand::DisplayText) {
        displayTextSize = text.length() + 1;
        memcpy(displayMessage->displayText, text.c_str(), displayTextSize);
    }

    message->messageSize = displayTextSize + sizeof(DisplayCommand);

    message->appPortSrc = appPort::DisplayApp;
    message->appPortDst = appPort::DisplayApp;

    message->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    message->addrDst = dst;

    return message;
}

void DisplayService::processReceivedMessage(messagePort port, DataMessage* message) {
    DisplayMessage* displayMessage = reinterpret_cast<DisplayMessage*>(message);
    if (displayMessage->messageSize < sizeof(DisplayCommand))
        return;

    switch (displayMessage->displayCommand) {
        case DisplayCommand::DisplayOn:
            displayOn(0);
            break;
        case DisplayCommand::DisplayOff:
            displayOff(0);
            break;
        case DisplayCommand::DisplayBlink:
            displayBlink(0);
            break;
        case DisplayCommand::DisplayClear:
            clearDisplay(0);
            break;
        case DisplayCommand::DisplayText:
            displayText(0, displayTextFrom(displayMessage->displayText,
                                           displayMessage->getDisplayTextSize())
                               .c_str());
            break;
        case DisplayCommand::DisplayLogo:
            displayLogo(0);
            break;
        default:
            break;
    }
}

void DisplayService::refreshShownLines() {
    std::lock_guard<std::mutex> lock(linesMutex_);
    if (shown_.size() == lines_.size() && shownVersion_ == lines_.version()) return;
    shown_.resize(lines_.size());
    for (size_t i = 0; i < lines_.size(); i++) {
        const std::string& text = lines_.line(i);
        if (shown_[i].text == text.c_str()) continue;
        shown_[i].text = text.c_str();
        setupTextMovement(shown_[i]);
    }
    shownVersion_ = lines_.version();
}

void DisplayService::drawDisplay() {
    refreshShownLines();
    display.clearDisplay();
    for (size_t i = 0; i < shown_.size(); ++i) {
        printLine(shown_[i], static_cast<int>(i) * LINE_HEIGHT);
    }
    display.display();
}

void DisplayService::printLine(ShownLine& line, int y) {
    display.setTextSize(1);
    display.setCursor(line.x, y);
    display.print(line.text);

    if (line.moving) {
        line.x -= 2;
        if (line.x < line.minX) line.x = display.width();
    }
}

void DisplayService::addText(const String& text) {
    if (!initialized) {
        ESP_LOGW(DISPLAY_TAG, "Display Service not initialized");
        return;
    }

    std::lock_guard<std::mutex> lock(linesMutex_);
    lines_.push(text.c_str());
}

void DisplayService::setupTextMovement(ShownLine& line) {
    if (line.text.length() > SCROLL_ABOVE_CHARS) {
        line.x = display.width();
        line.minX = -CHAR_WIDTH * static_cast<int>(line.text.length());
        line.moving = true;
    } else {
        line.x = 0;
        line.minX = 0;
        line.moving = false;
    }
}

void DisplayService::setTitle() {
    String title = "LoRaChat " + String(esp_ota_get_app_description()->version);
    String device_id =
        "Device ID: " + String(LoRaMeshService::getInstance().getLocalAddress(), HEX);

    std::lock_guard<std::mutex> lock(linesMutex_);
    lines_.setStatic(0, title.c_str());
    lines_.setStatic(1, device_id.c_str());
}
