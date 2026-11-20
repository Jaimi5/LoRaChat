#pragma once

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmdCatalogue.h"
#include "cmdTyped.h"
#include "installMode.h"
#include "maintWindowPlan.h"
#include "reportInterval.h"
#include "sleepMode.h"

inline CommandSpec spec(const std::string& name, Perm perm, const std::string& usage = "",
                        const std::vector<CommandCode>& codes = {}) {
    CommandSpec s;
    s.name = name;
    s.perm = perm;
    s.usage = usage;
    s.codes = codes;
    return s;
}

/** @return @p base with the typed declaration of a command that has an id. */
inline CommandSpec typed(uint8_t id, CommandSpec base, const std::string& args,
                         const std::string& reply, const std::string& replyTemplate) {
    base.id = id;
    base.args = args;
    base.reply = reply;
    base.replyTemplate = replyTemplate;
    return base;
}

/**
 * The commands of a gateway as the services in src/ register them. A host test
 * (test/host/test_cmd_catalogue_mirror.py) checks that the names, the ids, the schemas and the
 * templates match the sources.
 */
inline CommandCatalogue firmwareCatalogue() {
    const std::vector<CommandCode> setting = {
        {128, "The new image is still being verified. Try again later"},
        {129, "The setting could not be stored"},
        {130, "The setting is stored. The restart could not be scheduled"}};
    const std::vector<CommandCode> display = {{128, "The node has no working display"},
                                              {129, "The text is too long to send to another node"},
                                              {130, "The command could not be sent on"}};
    const std::string n = std::to_string(INSTALL_DEFAULT_SECONDS);
    CommandCatalogue catalogue;
    const std::vector<CommandSpec> specs = {
        spec("/role", Perm::OPEN),
        spec("/role.set", Perm::SIGNED, "<gateway or sensor>", setting),
        spec("/sleep", Perm::OPEN),
        spec("/sleep.set", Perm::SIGNED, "<none, light or deep>", setting),
        spec("/sleep.fraction", Perm::SIGNED,
             "<percent from 0 to " + std::to_string(MAX_SLEEP_FRACTION_PERCENT) + ">", setting),
        spec("/power", Perm::OPEN),
        typed(0x18,
              spec("/sensor", Perm::OPEN, "",
                   {{128, "No sensor found (BME280 or BMP280 at 0x76 or 0x77)"},
                    {129, "The sensor gave no measurement"},
                    {130, "This firmware has no sensor support"}}),
              "", "chip:e(BMP280,BME280) addr:h1 t:i/100 p:u/100 rh:u?/100 age:u?",
              "{0} at 0x{1}: {2} C[ {4} %RH] {3} hPa[, read {5} s ago, the sensor does not "
              "answer now]"),
        spec("/identify", Perm::OPEN, "[seconds] (default 10, up to 600)"),
        spec("/key", Perm::OPEN),
        spec("/key.set", Perm::LOCAL_ONLY, "<64 hex digits>",
             {{128, "The key could not be stored"}}),
        spec("/maint.open", Perm::SIGNED,
             "<seconds> [ap] (up to " + std::to_string(MaintWindowPlan::MAX_PULL_S) + ", or " +
                 std::to_string(MaintWindowPlan::MAX_AP_S) + " with ap)",
             {{128, "Gateways keep their own WiFi. The access point is for sensors"},
              {129, "The new image is still being verified. Try again later"},
              {130, "The node has no deployment key"},
              {131, "Refused, battery or memory too low"},
              {132, "Too many windows in the last 24 h"},
              {133, "The open window did not close"},
              {134, "The window could not be started"},
              {135, "The gateway WiFi is not connected"},
              {136, "An update check is already running"}}),
        spec("/maint.close", Perm::SIGNED),
        typed(0x34, spec("/version", Perm::OPEN), "",
              "ver:v slot:e(app0,app1)? state:e(VALID,PENDING_VERIFY) bl:h3 "
              "env:e(tbeam,tbeam-debug)?",
              "{0}[ {1}] {2} bl={3}[ env={4}]"),
        spec("/ota.status", Perm::OPEN),
        spec("/reboot", Perm::SIGNED, "", {{128, "The restart could not be scheduled"}}),
        spec("/ota.server", Perm::LOCAL_ONLY, "[http://host:port/folder/ or default]",
             {{128, "The server could not be stored"}}),
        spec("/maint.wifi", Perm::SIGNED,
             "<credentials encrypted by lmcmd.py wifi>, or on serial <ssid> [<password of 8 to "
             "63 characters>]",
             {{128, "The WiFi credentials are not valid"},
              {129, "A maintenance window is open. Close it first"},
              {130, "The WiFi credentials could not be stored"}}),
        spec("/help", Perm::OPEN),
        spec("/cmds", Perm::OPEN, "[page]"),
        spec("/getRT", Perm::OPEN),
        spec("/addSSID", Perm::LOCAL_ONLY, "<ssid of 1 to 32 bytes>"),
        spec("/addPassword", Perm::LOCAL_ONLY, "<password of at most 64 bytes>"),
        spec("/connectWiFi", Perm::SIGNED, "", {{128, "The WiFi did not connect"}}),
        spec("/getIP", Perm::OPEN, "", {{128, "The node has no IP address"}}),
        spec("/resetWiFiData", Perm::SIGNED, "", {{128, "The WiFi settings could not be reset"}}),
        spec("/install", Perm::SIGNED,
             "[seconds] (default " + n + ", 0 stops, up to " + std::to_string(INSTALL_MAX_SECONDS) +
                 ")"),
        spec("/report", Perm::OPEN),
        typed(0x7A,
              spec("/report.set", Perm::SIGNED,
                   "<seconds from " + std::to_string(REPORT_INTERVAL_MIN_S) + " to " +
                       std::to_string(REPORT_INTERVAL_MAX_S) + ", or default>",
                   {{128, "The interval could not be stored"}}),
              "seconds:u", "def:u?", "Report interval set to {a0} s[ (the default, {0} s)]"),
        spec("/displayOn", Perm::OPEN, "[<addr>]", display),
        spec("/displayOff", Perm::OPEN, "[<addr>]", display),
        spec("/displayBlink", Perm::OPEN, "[<addr>]", display),
        spec("/displayClear", Perm::OPEN, "[<addr>]", display),
        spec("/displayLogo", Perm::OPEN, "[<addr>]", display),
        spec("/displayText", Perm::OPEN, "[<addr>] <text>", display),
    };
    for (const CommandSpec& s : specs) EXPECT_TRUE(catalogue.add(s)) << s.name;
    return catalogue;
}

/** @return the typed declaration of command @p name of the firmware catalogue. */
inline TypedDeclaration firmwareDeclaration(const std::string& name) {
    const CommandCatalogue catalogue = firmwareCatalogue();
    const CommandSpec* found = catalogue.find(name);
    TypedDeclaration declaration;
    EXPECT_NE(found, nullptr) << name;
    if (found != nullptr) {
        EXPECT_TRUE(parseTypedDeclaration(found->id, found->args, found->reply,
                                          found->replyTemplate, declaration))
            << name;
    }
    return declaration;
}
