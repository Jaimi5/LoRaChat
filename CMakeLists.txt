cmake_minimum_required(VERSION 3.16.0)

# ESP-IDF components that nothing in the firmware requires. console, openthread and
# ieee802154 stay because protocomm and lwip require them.
set(EXCLUDE_COMPONENTS
    asio cbor cmock coap esp_https_server esp_lcd esp_local_ctrl esp_websocket_client expat
    freemodbus jsmn libsodium perfmon ulp unity)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LoRaChat)
