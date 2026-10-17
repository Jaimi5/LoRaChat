// Copied from LoRaMesherWeb ota/vectors/report_vectors.json (make_vectors.py there).
// test/host/test_report_vectors.py checks that they still match. Do not edit.
#pragma once

static const char* REPORT_KEY_HEX = "b447f630d75b083905828965f9b74ee134b8e15bf3a8e2c5bbc9b10328fd148f";
static const char* REPORT_NOOP_BODY =
    "{\"v\":1,\"node\":\"7680\",\"mac\":\"08:3a:f2:aa:bb:cc\",\"env\":\"tbeam\",\"role\":\"sensor\",\"ver\":\"0.1.0+g4434b51\",\"part\":\"app0\",\"state\":\"VALID\",\"boot\":12,\"rr\":\"POWERON\",\"bl\":\"f43854\",\"event\":\"noop\",\"decision\":1,\"rssi\":-77,\"heap_free\":213000,\"heap_min\":202000,\"batt_mv\":4110,\"vbus\":true,\"uptime_s\":9}";
static const char* REPORT_NOOP_TAG = "da84a501129851ee0fbac72b7dd859b9e91f55be5f8bc5532089bf2d5e190485";
static const char* REPORT_ROLLBACK_BODY =
    "{\"v\":1,\"node\":\"5E9C\",\"env\":\"tbeam\",\"ver\":\"0.1.0+g4434b51\",\"part\":\"app1\",\"state\":\"VALID\",\"boot\":4,\"rr\":\"SW\",\"event\":\"rollback\",\"last_invalid\":\"app0\",\"reason\":5,\"sha\":\"0011223344556677\"}";
