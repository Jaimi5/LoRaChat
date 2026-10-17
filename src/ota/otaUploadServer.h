#pragma once

#include "esp_http_server.h"

/**
 * @brief Upload page served on the node's maintenance access point (http://192.168.4.1/).
 *
 * POST /update takes a bundle (signed manifest followed by the image, bundle.bin from
 * release.py) as the raw request body. The manifest is checked before the first flash write;
 * the image then goes through the same writer and checks as the WiFi pull. On success the
 * node reboots into the new image. GET /status returns the running version as JSON.
 */
class OtaUploadServer {
public:
    /** Starts the HTTP server on port 80. */
    static bool start();

    /** Stops the server. */
    static void stop();

private:
    static esp_err_t handlePage(httpd_req_t* req);
    static esp_err_t handleStatus(httpd_req_t* req);
    static esp_err_t handleUpdate(httpd_req_t* req);

    static httpd_handle_t server_;
};
