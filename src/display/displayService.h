#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include <Wire.h>
#include <atomic>
#include <mutex>

#include "devices/initDevices.h"
#include "message/messageService.h"

#include "displayCommandService.h"
#include "displayMessage.h"
#include "statusScreen.h"

#include "config.h"

/** Errors of the display commands. */
enum DisplayError : uint8_t {
    DISPLAY_ERR_NO_DISPLAY = 128,
    /** The argument is the longest text in characters. */
    DISPLAY_ERR_TEXT_TOO_LONG = 129,
    DISPLAY_ERR_NOT_SENT = 130,
};

/**
 * @brief SSD1306 display: one status screen, redrawn when its content changes.
 *
 * Commands may come from any task. They only change shared state or post a request.
 * DisplayTask alone talks to the panel.
 */
class DisplayService : public MessageService {
public:
    static DisplayService& getInstance() {
        static DisplayService instance;
        return instance;
    }

    /**
     * @brief Starts the panel and its task. The panel stays on for DISPLAY_AWAKE_MS.
     * @param gateway the node is a gateway: the screen shows WiFi and MQTT.
     */
    void init(bool gateway);

    /** Turns the display on for DISPLAY_AWAKE_MS and shows the status screen. */
    void wake();

    /** @return true while the display is on. */
    bool isAwake() const { return ready_ && esp_timer_get_time() < sleepAtUs_; }

    /**
     * @brief Runs a display command on this node.
     * @param text the line of a DisplayText command.
     * @return success, ARG for a command that does not exist, or DISPLAY_ERR_NO_DISPLAY.
     */
    TypedResult run(DisplayCommand command, const String& text = "");

    /**
     * @brief Sends a display command to node @p dst as a display message, which gets no reply.
     * @return a text that says it was sent, DISPLAY_ERR_TEXT_TOO_LONG or DISPLAY_ERR_NOT_SENT.
     */
    CommandReply sendRemote(DisplayCommand command, uint16_t dst, const String& text);

    String getJSON(DataMessage* message) override;

    DataMessagePtr getDataMessage(JsonObject data) override;

    /**
     * @return the message for @p command, or nullptr if @p text is longer than one radio
     *         frame takes.
     */
    DataMessagePtr getDisplayMessage(DisplayCommand command, uint16_t dst, const String& text = "");

    /** @return the longest text /displayText sends to another node in one message. */
    static size_t maxRemoteTextLength();

    void processReceivedMessage(messagePort port, DataMessage* message) override;

private:
    DisplayService() : MessageService(DisplayApp, "Display") { commandService = &commands_; }

    DisplayCommandService commands_;

    /** Work that DisplayTask does on behalf of other tasks. */
    enum PanelRequest : uint32_t {
        PANEL_BLINK = 1 << 0,
        PANEL_LOGO = 1 << 1,
        PANEL_STATUS = 1 << 2,
    };

    /** What the panel shows in place of the status screen. DisplayTask only. */
    enum class Overlay : uint8_t { NONE, LOGO, BLINK };

    Adafruit_SSD1306 display_ = Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RST);

    TaskHandle_t taskHandle_ = nullptr;
    /** Set once the task exists. Before that every command for this node is refused. */
    std::atomic<bool> ready_{false};
    /** esp_timer time at which the display goes to sleep. */
    std::atomic<int64_t> sleepAtUs_{0};
    std::atomic<uint32_t> requests_{0};

    std::mutex textMutex_;
    char text_[SCREEN_ROW_CHARS + 1] = {};
    int64_t textAtUs_ = 0;

    bool gateway_ = false;
    uint16_t address_ = 0;
    char version_[sizeof(StatusSnapshot::version)] = {};

    /** Posts @p request to DisplayTask and turns the display on so it is seen. */
    void requestPanel(PanelRequest request);
    void setText(const char* text);

    static void displayTask(void* pvParameters);

    // DisplayTask only.
    bool panelOn_ = true;
    Overlay overlay_ = Overlay::NONE;
    int64_t overlayStepAtUs_ = 0;
    uint8_t blinkStepsLeft_ = 0;
    ScreenModel drawn_{};
    bool drawnValid_ = false;
    MeshState observedState_ = MeshState::INITIALIZING;
    int64_t stateSinceUs_ = 0;
    uint32_t ticks_ = 0;
    PowerState power_;
    bool powerKnown_ = false;

    void runTask();
    void sleepPanel();
    void startOverlay(Overlay overlay);
    TickType_t stepOverlay(int64_t nowUs);
    void drawLogo();
    void refreshStatus();
    void fillSnapshot(StatusSnapshot& snapshot);
    void drawModel(const ScreenModel& model);
};
