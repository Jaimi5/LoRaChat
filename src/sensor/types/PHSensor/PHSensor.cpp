#include "PHSensor.h"

static const char* PH_SENSOR_TAG = "SHT4x";

void PHSensor::init() {
    ESP_LOGV(PH_SENSOR_TAG, "Initializing PH Sensor");

    initialized = false;
    // Wire.begin();

    // initialized = true;
}

PHSensorMessage PHSensor::read() {
    return PHSensorMessage(-1, -1);

    // if (!initialized) {
    //     ESP_LOGE(PH_SENSOR_TAG, "PH Sensor not initialized");
    //     return PHSensorMessage(-1, -1);
    // }

    // PH.send_read_cmd();
    // RTD.send_read_cmd();

    // vTaskDelay(1000 / portTICK_PERIOD_MS);

    // float ph = receive_reading(PH);

    // float rtd = receive_reading(RTD);

    // ESP_LOGI(PH_SENSOR_TAG, "PH: %f, Temperature", ph, rtd);

    // return PHSensorMessage(rtd, ph);
}

// float PHSensor::receive_reading(Ezo_board& board) {
//     // function to decode the reading after the read command was issued

//     ESP_LOGV(PH_SENSOR_TAG, "Receiving reading from %s", board.get_name());

//     board.receive_read_cmd();  //get the response data and put it into the [board].reading variable if successful

//     switch (board.get_error())  //switch case based on what the response code is.
//     {
//         case Ezo_board::SUCCESS:
//             return board.get_last_received_reading();
//             break;

//         case Ezo_board::FAIL:
//             ESP_LOGE(PH_SENSOR_TAG, "Failed to receive reading from %s", board.get_name());
//             break;

//         case Ezo_board::NOT_READY:
//             ESP_LOGE(PH_SENSOR_TAG, "%s is not ready for another command yet", board.get_name());
//             break;

//         case 3: // NO DATA
//             ESP_LOGE(PH_SENSOR_TAG, "No data received from %s", board.get_name());
//             break;
//     }

//     ESP_LOGE(PH_SENSOR_TAG, "Error receiving reading from %s", board.get_name());
//     return 0;
// }