#include "displayService.h"
#include "images.h"

#include <cmath>
#include <cstring>

#include "displayText.h"

#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "loramesh/loraMeshService.h"
#include "message/messageManager.h"
#include "monitor/monService.h"
#include "mqtt/mqttService.h"
#include "sensor/bme280.h"
#include "wifi/wifiServerService.h"

static const char* DISPLAY_TAG = "DisplayService";
static constexpr int64_t US_PER_MS = 1000;
static constexpr int64_t US_PER_S = 1000000;
static constexpr uint32_t REFRESH_MS = 1000;
static constexpr int64_t LOGO_HOLD_US = 1000 * US_PER_MS;
static constexpr int64_t BLINK_HALF_PERIOD_US = 500 * US_PER_MS;
static constexpr uint8_t BLINK_STEPS = 10;
/** Status refreshes between two readings of the PMU. */
static constexpr uint32_t POWER_READ_EVERY = 5;
/** A sensor reading older than this is not shown. */
static constexpr uint32_t READING_MAX_AGE_S = 900;
static constexpr int CHAR_WIDTH = 6;
static constexpr int ROW_HEIGHT = 8;
static constexpr int HEADER_TEXT_X = 2 * CHAR_WIDTH;
static constexpr int BIG_LINE_Y = ROW_HEIGHT;
static constexpr int FIRST_ROW_Y = 3 * ROW_HEIGHT;
static const char* const COMMAND_NAMES[] = {"On", "Off", "Blink", "Clear", "Text", "Logo"};

void DisplayService::init(bool gateway) {
    ESP_LOGV(DISPLAY_TAG, "Initializing Display Service");

#if DISPLAY_RST >= 0
    pinMode(DISPLAY_RST, OUTPUT);
    digitalWrite(DISPLAY_RST, LOW);
    delay(20);
    digitalWrite(DISPLAY_RST, HIGH);
#endif

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display_.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, false, false)) {
        ESP_LOGE(DISPLAY_TAG, "SSD1306 allocation failed");
        return;
    }
    display_.setTextWrap(false);
    display_.clearDisplay();
    display_.display();

    gateway_ = gateway;
    address_ = LoRaMeshService::getInstance().getLocalAddress();
    strlcpy(version_, esp_ota_get_app_description()->version, sizeof(version_));

    sleepAtUs_ = esp_timer_get_time() + static_cast<int64_t>(DISPLAY_AWAKE_MS) * US_PER_MS;
    if (esp_reset_reason() == ESP_RST_POWERON) requests_ = PANEL_LOGO;

    if (xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, this, 3, &taskHandle_, 0) !=
        pdPASS) {
        ESP_LOGE(DISPLAY_TAG, "Display task creation failed");
        return;
    }
    ready_ = true;
}

void DisplayService::wake() {
    requestPanel(PANEL_STATUS);
}

void DisplayService::requestPanel(PanelRequest request) {
    if (!ready_) return;
    requests_.fetch_or(request);
    sleepAtUs_ = esp_timer_get_time() + static_cast<int64_t>(DISPLAY_AWAKE_MS) * US_PER_MS;
    xTaskNotifyGive(taskHandle_);
}

void DisplayService::setText(const char* text) {
    std::lock_guard<std::mutex> lock(textMutex_);
    strlcpy(text_, text, sizeof(text_));
    textAtUs_ = esp_timer_get_time();
}

String DisplayService::execute(DisplayCommand command, uint16_t dst, const String& text) {
    if (command > DisplayCommand::DisplayLogo) return "Unknown display command";
    if (dst != 0 && dst != LoRaMeshService::getInstance().getLocalAddress()) {
        return sendRemote(command, dst, text);
    }
    if (!ready_) return "Display Service not initialized";

    switch (command) {
        case DisplayCommand::DisplayOn:
            wake();
            break;
        case DisplayCommand::DisplayOff:
            sleepAtUs_ = 0;
            xTaskNotifyGive(taskHandle_);
            break;
        case DisplayCommand::DisplayBlink:
            requestPanel(PANEL_BLINK);
            break;
        case DisplayCommand::DisplayClear:
            setText("");
            break;
        case DisplayCommand::DisplayText:
            setText(text.c_str());
            break;
        case DisplayCommand::DisplayLogo:
            requestPanel(PANEL_LOGO);
            break;
    }
    return String("Display ") + COMMAND_NAMES[command];
}

String DisplayService::sendRemote(DisplayCommand command, uint16_t dst, const String& text) {
    if (command == DisplayCommand::DisplayText && text.length() > maxRemoteTextLength()) {
        return "Text longer than " + String(maxRemoteTextLength()) + " characters";
    }
    DataMessagePtr message = getDisplayMessage(command, dst, text);
    if (!message) return "No memory for the message";
    MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, message.get());
    return String("Send Display ") + COMMAND_NAMES[command];
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
                                                 const String& text) {
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

    String text;
    if (displayMessage->displayCommand == DisplayCommand::DisplayText) {
        text = displayTextFrom(displayMessage->displayText, displayMessage->getDisplayTextSize())
                   .c_str();
    }
    execute(displayMessage->displayCommand, 0, text);
}

void DisplayService::displayTask(void* pvParameters) {
    static_cast<DisplayService*>(pvParameters)->runTask();
}

void DisplayService::runTask() {
    for (;;) {
        int64_t nowUs = esp_timer_get_time();
        int64_t awakeUs = sleepAtUs_ - nowUs;
        if (awakeUs <= 0) {
            sleepPanel();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!panelOn_) {
            display_.ssd1306_command(SSD1306_DISPLAYON);
            panelOn_ = true;
            ESP_LOGI(DISPLAY_TAG, "Display awake");
        }

        uint32_t requests = requests_.exchange(0);
        if (requests & PANEL_STATUS) {
            overlay_ = Overlay::NONE;
            drawnValid_ = false;
        }
        if (requests & PANEL_LOGO) startOverlay(Overlay::LOGO);
        if (requests & PANEL_BLINK) startOverlay(Overlay::BLINK);

        TickType_t wait = pdMS_TO_TICKS(REFRESH_MS);
        if (overlay_ != Overlay::NONE) wait = stepOverlay(nowUs);
        if (overlay_ == Overlay::NONE) refreshStatus();

        TickType_t untilSleep = pdMS_TO_TICKS(awakeUs / US_PER_MS) + 1;
        ulTaskNotifyTake(pdTRUE, wait < untilSleep ? wait : untilSleep);
    }
}

void DisplayService::sleepPanel() {
    if (!panelOn_) return;
    display_.ssd1306_command(SSD1306_DISPLAYOFF);
    panelOn_ = false;
    overlay_ = Overlay::NONE;
    drawnValid_ = false;
    ESP_LOGI(DISPLAY_TAG, "Display sleeping");
    ESP_LOGD(DISPLAY_TAG, "DisplayTask stack high water: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void DisplayService::startOverlay(Overlay overlay) {
    overlay_ = overlay;
    overlayStepAtUs_ = 0;
    blinkStepsLeft_ = BLINK_STEPS;
}

/**
 * Draws the next step of the logo or of the blink when it is due.
 * @return the ticks until the following step. The overlay is NONE once it has ended.
 */
TickType_t DisplayService::stepOverlay(int64_t nowUs) {
    if (nowUs >= overlayStepAtUs_) {
        bool first = overlayStepAtUs_ == 0;
        if (overlay_ == Overlay::LOGO && first) {
            drawLogo();
            overlayStepAtUs_ = nowUs + LOGO_HOLD_US;
        } else if (overlay_ == Overlay::BLINK && blinkStepsLeft_ > 0) {
            // The panel is lit on the even steps and dark on the odd ones.
            display_.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                              blinkStepsLeft_ % 2 == 0 ? WHITE : BLACK);
            display_.display();
            blinkStepsLeft_--;
            overlayStepAtUs_ = nowUs + BLINK_HALF_PERIOD_US;
        } else {
            overlay_ = Overlay::NONE;
            drawnValid_ = false;
            return 0;
        }
    }
    return pdMS_TO_TICKS((overlayStepAtUs_ - nowUs) / US_PER_MS) + 1;
}

void DisplayService::drawLogo() {
    static const char TITLE[] = "LoRaChat";
    display_.clearDisplay();
    display_.setTextSize(1);
    display_.setTextColor(WHITE);
    display_.setCursor((DISPLAY_WIDTH - static_cast<int>(strlen(TITLE)) * CHAR_WIDTH) / 2, 0);
    display_.print(TITLE);
    display_.drawXBitmap((DISPLAY_WIDTH - LOGO_WIDTH) / 2, 15, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT,
                         WHITE);
    display_.display();
}

void DisplayService::refreshStatus() {
    StatusSnapshot snapshot;
    fillSnapshot(snapshot);
    ScreenModel model{};
    buildStatusScreen(snapshot, model);
    if (drawnValid_ && memcmp(&model, &drawn_, sizeof(model)) == 0) return;
    drawModel(model);
    drawn_ = model;
    drawnValid_ = true;
}

void DisplayService::fillSnapshot(StatusSnapshot& snapshot) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    int64_t nowUs = esp_timer_get_time();

    snapshot.gateway = gateway_;
    snapshot.address = address_;
    memcpy(snapshot.version, version_, sizeof(snapshot.version));
    snapshot.uptimeSeconds = static_cast<uint32_t>(nowUs / US_PER_S);

    if (ticks_++ % POWER_READ_EVERY == 0) powerKnown_ = InitDevices::readPower(power_);
    snapshot.powerKnown = powerKnown_;
    snapshot.batteryPresent = power_.batteryPresent;
    snapshot.externalPower = power_.externalPower;
    snapshot.batteryMv = power_.batteryMv;

    MeshStatus status = mesh.getMeshStatus();
    if (status.state != observedState_) {
        observedState_ = status.state;
        stateSinceUs_ = nowUs;
    }
    snapshot.meshState = status.state;
    snapshot.stateSeconds = static_cast<uint32_t>((nowUs - stateSinceUs_) / US_PER_S);
    snapshot.syncKnown = status.synchronized;
    snapshot.connectedNodes = status.connectedNodes;

    std::vector<loramesher::RouteEntry> routes = mesh.getRoutingTableEntries();
    uint32_t heardAgeMs = 0;
    if (mesh.getLastHeardAgeMs(routes, heardAgeMs)) statusNoteHeard(snapshot, heardAgeMs / 1000);

    snapshot.hasRoute = !gateway_ && mesh.getGatewayRoute(snapshot.nextHop, snapshot.gatewayHops);
    for (const auto& route : routes) {
        // The mesh keeps signal readings of direct neighbours only.
        if (!route.is_valid || route.destination != route.next_hop) continue;
        StatusNeighbour neighbour;
        neighbour.address = route.destination;
        neighbour.signalKnown = route.last_rssi != 0.0F;
        neighbour.rssiDbm = static_cast<int16_t>(lroundf(route.last_rssi));
        neighbour.snrDb = route.last_snr;
        neighbour.ageSeconds = route.last_seen_ms / 1000;
        statusAddNeighbour(snapshot, neighbour);
        if (snapshot.hasRoute && route.destination == snapshot.nextHop) {
            snapshot.nextHopSnrKnown = neighbour.signalKnown;
            snapshot.nextHopSnrDb = neighbour.snrDb;
        }
    }

    if (gateway_) {
#ifdef WIFI_ENABLED
        int8_t rssi = 0;
        snapshot.wifiConnected = WiFiServerService::getInstance().getRssi(rssi);
        snapshot.wifiRssiDbm = rssi;
#endif
#ifdef MQTT_ENABLED
        snapshot.mqttConnected = MqttService::getInstance().isDeviceConnected();
#endif
    }

    Bme280Reading reading;
    uint32_t readingAgeS = 0;
    if (Bme280Sensor::getInstance().lastReading(reading, readingAgeS) &&
        readingAgeS < READING_MAX_AGE_S) {
        snapshot.hasTemperature = true;
        snapshot.temperatureC = static_cast<float>(reading.tempCentiC) / 100.0F;
        snapshot.hasHumidity = reading.hasHumidity;
        snapshot.humidityPercent = static_cast<float>(reading.rhCentiPercent) / 100.0F;
        snapshot.hasPressure = true;
        snapshot.pressureHpa = static_cast<float>(reading.pressurePa) / 100.0F;
    }

#ifdef MQTT_MON_ENABLED
    snapshot.installSeconds = MonService::getInstance().installRemainingS();
#endif

    std::lock_guard<std::mutex> lock(textMutex_);
    memcpy(snapshot.text, text_, sizeof(snapshot.text));
    snapshot.textSeconds = static_cast<uint32_t>((nowUs - textAtUs_) / US_PER_S);
}

void DisplayService::drawModel(const ScreenModel& model) {
    const uint8_t* icon = ICON_JOINED;
    switch (model.icon) {
        case ScreenIcon::SEARCHING:
            icon = ICON_SEARCHING[model.iconFrame % SCREEN_SEARCH_FRAMES];
            break;
        case ScreenIcon::JOINING:
            icon = ICON_JOINING;
            break;
        case ScreenIcon::FAULT:
            icon = ICON_FAULT;
            break;
        case ScreenIcon::GATEWAY:
            icon = ICON_GATEWAY;
            break;
        case ScreenIcon::INSTALL:
            icon = ICON_INSTALL;
            break;
        case ScreenIcon::JOINED:
            break;
    }

    display_.clearDisplay();
    uint16_t headerColor = WHITE;
    if (model.invertHeader) {
        display_.fillRect(0, 0, DISPLAY_WIDTH, ROW_HEIGHT, WHITE);
        headerColor = BLACK;
    }
    display_.drawBitmap(0, 0, icon, ICON_SIZE, ICON_SIZE, headerColor);
    display_.setTextSize(1);
    display_.setTextColor(headerColor);
    display_.setCursor(HEADER_TEXT_X, 0);
    display_.print(model.header);

    display_.setTextColor(WHITE);
    display_.setTextSize(2);
    display_.setCursor(0, BIG_LINE_Y);
    display_.print(model.big);

    display_.setTextSize(1);
    for (size_t i = 0; i < SCREEN_ROWS; i++) {
        display_.setCursor(0, FIRST_ROW_Y + static_cast<int>(i) * ROW_HEIGHT);
        display_.print(model.rows[i]);
    }
    display_.display();
}
