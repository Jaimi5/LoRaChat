#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "esp_timer.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "displayCommandService.h"

#include "displayLines.h"
#include "displayMessage.h"

#include "config.h"

#include "loramesh/loraMeshService.h"

/**
 * @brief SSD1306 display: fixed title lines and a log of received text.
 *
 * Commands may come from any task. They only change the line buffer (under a lock) or post a
 * request; DisplayTask alone talks to the panel.
 */
class DisplayService : public MessageService {
public:
    static DisplayService& getInstance() {
        static DisplayService instance;
        return instance;
    }

    void init();

    /** Turns the display on for DISPLAY_AWAKE_MS; it then goes to sleep. */
    void wake();

    /** @return true while the display is on. */
    bool isAwake() const { return initialized && esp_timer_get_time() < sleepAtUs; }

    String displayOn(uint16_t dst);

    String displayOff(uint16_t dst);

    String displayBlink(uint16_t dst);

    String clearDisplay(uint16_t dst);

    String displayLogo(uint16_t dst, uint16_t src = 0);

    String displayText(uint16_t dst, String text, uint16_t src = 0);

    String getJSON(DataMessage* message) override;

    DataMessagePtr getDataMessage(JsonObject data) override;

    /**
     * @return the message for @p command, or nullptr if @p text is longer than one radio
     *         frame takes.
     */
    DataMessagePtr getDisplayMessage(DisplayCommand command, uint16_t dst, String text = "");

    /** @return the longest text /displayText sends to another node in one message. */
    static size_t maxRemoteTextLength();

    void processReceivedMessage(messagePort port, DataMessage* message) override;

private:
    DisplayService() : MessageService(DisplayApp, "Display") { commandService = &commands_; }

    DisplayCommandService commands_;

    /** Panel work that DisplayTask does on behalf of other tasks. */
    enum PanelRequest : uint32_t {
        PANEL_BLINK = 1 << 0,
        PANEL_LOGO = 1 << 1,
    };

    /** Scroll state of one drawn line; owned by DisplayTask. */
    struct ShownLine {
        String text;
        int x = 0;
        int minX = 0;
        bool moving = false;
    };

    Adafruit_SSD1306 display = Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RST);

    TaskHandle_t display_TaskHandle = nullptr;

    void createDisplayTask();

    static void displayTask(void* pvParameters);

    static constexpr size_t MAX_LINES = 7;     // Maximum number of lines the display can handle
    static constexpr int LINE_HEIGHT = 9;      // Height of each line of text
    static constexpr size_t STATIC_LINES = 3;  // Number of static lines at the top

    std::mutex linesMutex_;
    DisplayLines lines_{STATIC_LINES, MAX_LINES};

    /** Sends a request to DisplayTask and wakes the display so it is seen. */
    void requestPanel(PanelRequest request);
    void addText(const String& text);
    void setTitle();

    // DisplayTask only.
    std::vector<ShownLine> shown_;
    uint32_t shownVersion_ = 0;
    bool panelOn = true;
    void refreshShownLines();
    void drawDisplay();
    void printLine(ShownLine& line, int y);
    void setupTextMovement(ShownLine& line);
    void drawLogo(uint16_t src);
    void blink();

    // esp_timer time at which the display goes to sleep
    std::atomic<int64_t> sleepAtUs{0};
    std::atomic<uint32_t> panelRequests_{0};
    std::atomic<uint16_t> logoSrc_{0};
    std::atomic<bool> displayOnFlag{true};
    std::atomic<bool> initialized{false};
};
