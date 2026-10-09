// Bench bridge: joins a node's maintenance access point and forwards HTTP requests whose body
// arrives over USB serial. Line protocol at 921600 baud (driven by scripts/ota_tools/ap_upload.py):
//   PING                            -> PONG
//   JOIN <ssid> <password>          -> OK JOINED <ip> RSSI <dBm> | ERR join
//   GET <path>                      -> RESPONSE, the raw HTTP response, END
//   POST <path> <length> <send>     -> READY; then blocks of up to 4096 bytes, each answered by
//                                      ACK <total>; after <send> bytes: CUT if <send> < <length>
//                                      (connection closed), else RESPONSE ... END
#include <Arduino.h>
#include <WiFi.h>

static const char* NODE_IP = "192.168.4.1";
static constexpr size_t BLOCK_SIZE = 4096;
static constexpr uint32_t SERIAL_TIMEOUT_MS = 10000;
static constexpr uint32_t JOIN_TIMEOUT_MS = 30000;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 60000;
static constexpr uint32_t RESPONSE_IDLE_MS = 3000;

static uint8_t block[BLOCK_SIZE];
static WiFiClient client;

static String readLine() {
    String line;
    for (;;) {
        while (!Serial.available()) delay(1);
        char c = Serial.read();
        if (c == '\n') return line;
        if (c != '\r') line += c;
    }
}

static size_t readExact(uint8_t* buffer, size_t size) {
    size_t received = 0;
    uint32_t last = millis();
    while (received < size && millis() - last < SERIAL_TIMEOUT_MS) {
        int available = Serial.available();
        if (available <= 0) {
            delay(1);
            continue;
        }
        received += Serial.readBytes(buffer + received, min((size_t)available, size - received));
        last = millis();
    }
    return received;
}

static void printResponse() {
    uint32_t start = millis();
    while (!client.available() && client.connected() && millis() - start < RESPONSE_TIMEOUT_MS) {
        delay(10);
    }
    Serial.println("RESPONSE");
    uint32_t last = millis();
    while ((client.connected() || client.available()) && millis() - last < RESPONSE_IDLE_MS) {
        while (client.available()) {
            Serial.write(client.read());
            last = millis();
        }
        delay(5);
    }
    Serial.println();
    Serial.println("END");
    client.stop();
}

static void join(const String& args) {
    int space = args.indexOf(' ');
    String ssid = args.substring(0, space);
    String password = args.substring(space + 1);
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_TIMEOUT_MS) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERR join");
        return;
    }
    Serial.printf("OK JOINED %s RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void get(const String& path) {
    if (!client.connect(NODE_IP, 80)) {
        Serial.println("ERR connect");
        return;
    }
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), NODE_IP);
    printResponse();
}

static void post(const String& args) {
    int first = args.indexOf(' ');
    int second = args.indexOf(' ', first + 1);
    String path = args.substring(0, first);
    size_t length = args.substring(first + 1, second).toInt();
    size_t toSend = args.substring(second + 1).toInt();
    if (!client.connect(NODE_IP, 80)) {
        Serial.println("ERR connect");
        return;
    }
    client.printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/octet-stream\r\n"
                  "Content-Length: %u\r\nConnection: close\r\n\r\n",
                  path.c_str(), NODE_IP, (unsigned)length);
    Serial.println("READY");
    size_t sent = 0;
    while (sent < toSend) {
        size_t size = min(BLOCK_SIZE, toSend - sent);
        if (readExact(block, size) != size) {
            Serial.println("ERR serial");
            client.stop();
            return;
        }
        if (client.write(block, size) != size) {
            Serial.printf("ERR tcp after %u\n", (unsigned)sent);
            printResponse();
            return;
        }
        sent += size;
        Serial.printf("ACK %u\n", (unsigned)sent);
    }
    if (toSend < length) {
        client.stop();
        Serial.println("CUT");
        return;
    }
    printResponse();
}

void setup() {
    Serial.setRxBufferSize(2 * BLOCK_SIZE + 256);
    Serial.begin(921600);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    Serial.println("BRIDGE READY");
}

void loop() {
    String line = readLine();
    if (line == "PING") {
        Serial.println("PONG");
    } else if (line.startsWith("JOIN ")) {
        join(line.substring(5));
    } else if (line.startsWith("GET ")) {
        get(line.substring(4));
    } else if (line.startsWith("POST ")) {
        post(line.substring(5));
    } else if (line.length() > 0) {
        Serial.println("ERR unknown");
    }
}
