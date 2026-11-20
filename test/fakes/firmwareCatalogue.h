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
 * (test/host/test_cmd_catalogue_mirror.py) checks that the names, the ids, the schemas, the
 * templates and the errors match the sources.
 */
inline CommandCatalogue firmwareCatalogue() {
    const std::vector<CommandCode> setting = {
        {128, "The new image is still being verified. Try again later"},
        {129, "The setting could not be stored", "x"},
        {130, "The setting is stored. The restart could not be scheduled"}};
    const std::vector<CommandCode> display = {{128, "The node has no working display"},
                                              {129, "The text is too long to send to another node"},
                                              {130, "The command could not be sent on"}};
    const std::string n = std::to_string(INSTALL_DEFAULT_SECONDS);
    CommandCatalogue catalogue;
    const std::vector<CommandSpec> specs = {
        typed(0x10, spec("/role", Perm::OPEN), "", "role:e(sensor,gateway) src:e(default,stored)",
              "{0} ({1})"),
        typed(0x11, spec("/role.set", Perm::SIGNED, "<gateway or sensor>", setting),
              "role:e(sensor,gateway)", "", "Role set to {a0}, restarting"),
        typed(0x12, spec("/sleep", Perm::OPEN), "",
              "mode:e(none,light,deep) src:e(default,stored) frac:u? fdef:e(default)? "
              "light:u lights:u deep:u deeps:u keys:u?",
              "sleep {0} ({1}), fraction [{2} % (stored)][{3}], light {4} x {5} s, deep {6} "
              "x {7} s since power-on[, {8} light sleeps ended by the power key]"),
        typed(0x13, spec("/sleep.set", Perm::SIGNED, "<none, light or deep>", setting),
              "mode:e(none,light,deep)", "", "Sleep mode set to {a0}, restarting"),
        typed(0x14,
              spec("/sleep.fraction", Perm::SIGNED,
                   "<percent from 0 to " + std::to_string(MAX_SLEEP_FRACTION_PERCENT) + ">",
                   setting),
              "percent:u", "", "Sleep fraction set to {a0} %, restarting"),
        typed(0x16, spec("/power", Perm::OPEN), "",
              "pmu:e(none,AXP192,AXP2101) vbus:e(no,yes)? battery:e(no,yes)? mv:u? "
              "pct:i? charging:e(no,yes)? state:e(TRICKLE,PRE,CC,CV,DONE,STOP)? reg:r?",
              "pmu={0}[ vbus={1} battery={2} {3}mV {4}% charging={5}][ state={6}][ "
              "reg={7}]"),
        typed(0x18,
              spec("/sensor", Perm::OPEN, "",
                   {{128, "No sensor found (BME280 or BMP280 at 0x76 or 0x77)"},
                    {129, "The sensor gave no measurement"},
                    {130, "This firmware has no sensor support"}}),
              "", "chip:e(BMP280,BME280) addr:h1 t:i/100 p:u/100 rh:u?/100 age:u?",
              "{0} at 0x{1}: {2} C[ {4} %RH] {3} hPa[, read {5} s ago, the sensor does not "
              "answer now]"),
        typed(0x1A,
              spec("/identify", Perm::OPEN, "[seconds] (default 10, up to 600)",
                   {{128, "The blink task could not be started"}}),
              "seconds:u?", "seconds:u", "Blinking for {0} s"),
        typed(0x30, spec("/key", Perm::OPEN), "", "none:e(No)? fp:r?",
              "[{0} deployment key][Deployment key stored, fingerprint {1}]"),
        spec("/key.set", Perm::LOCAL_ONLY, "<64 hex digits>",
             {{128, "The key could not be stored"}}),
        typed(0x32,
              spec("/maint.open", Perm::SIGNED,
                   "<seconds> [ap] (up to " + std::to_string(MaintWindowPlan::MAX_PULL_S) +
                       ", or " + std::to_string(MaintWindowPlan::MAX_AP_S) + " with ap)",
                   {{128, "Gateways keep their own WiFi. The access point is for sensors"},
                    {129, "The new image is still being verified. Try again later"},
                    {130, "The node has no deployment key"},
                    {131, "Refused, battery or memory too low", "e(REFUSE_BATTERY,REFUSE_HEAP)"},
                    {132, "Too many windows in the last 24 h"},
                    {133, "The open window did not close"},
                    {134, "The window could not be started"},
                    {135, "The gateway WiFi is not connected"},
                    {136, "An update check is already running"}}),
              "seconds:u ap:e(ap)?", "left:u? pull:u? apfor:u? count:u? gw:e(gateway)? ssid:s?",
              "[Window extended, {0} s left][Pull window open for {1} s][Access point {5} "
              "open for {2} s, upload at http://192.168.4.1/][ ({3} of 8 in 24 h)][Checking "
              "the OTA server over the {4} WiFi]"),
        typed(0x33, spec("/maint.close", Perm::SIGNED), "", "none:e(No)? closing:e(Closing)?",
              "[{0} window open][{1} the window]"),
        typed(0x34, spec("/version", Perm::OPEN), "",
              "ver:v slot:e(app0,app1)? state:e(VALID,PENDING_VERIFY) bl:h3 "
              "env:e(tbeam,tbeam-debug)? git:h4 dirty:e(dirty)?",
              "{0}+g{5}[.{6}][ {1}] {2} bl={3}[ env={4}]"),
        typed(0x35, spec("/ota.status", Perm::OPEN), "",
              "last:e(NONE,VALID,ROLLED_BACK,BL_NO_ROLLBACK)? pending:e(no,yes) reason:u "
              "unexplained:u blacklisted:u",
              "last=[{0}] pending={1} reason={2} unexplained={3} blacklisted={4}"),
        typed(0x36,
              spec("/reboot", Perm::SIGNED, "", {{128, "The restart could not be scheduled"}}), "",
              "", "Rebooting"),
        spec("/ota.server", Perm::LOCAL_ONLY, "[http://host:port/folder/ or default]",
             {{128, "The server could not be stored"}}),
        typed(0x38,
              spec("/maint.wifi", Perm::SIGNED,
                   "<credentials encrypted by lmcmd.py wifi>, or on serial <ssid> [<password of 8 "
                   "to 63 characters>]",
                   {{128, "The WiFi credentials are not valid"},
                    {129, "A maintenance window is open. Close it first"},
                    {130, "The WiFi credentials could not be stored", "x"}}),
              "sealed:r", "re:e(reconnecting)? ssid:s", "WiFi credentials stored for {1}[, {0}]"),
        spec("/help", Perm::OPEN),
        spec("/cmds", Perm::OPEN, "[page]"),
        typed(0x70, spec("/getRT", Perm::OPEN), "",
              "n:u routes:*(dst:h2 via:h2 lq:u hops:u rssi:u? snr:i?/100)",
              "Routing table, {0} routes[: {1*{0} (hops:{3} lq:{2}[ rssi:-{4}][ "
              "snr:{5}]) - Via: {1}*, }]"),
        spec("/addSSID", Perm::LOCAL_ONLY, "<ssid of 1 to 32 bytes>"),
        spec("/addPassword", Perm::LOCAL_ONLY, "<password of at most 64 bytes>"),
        typed(0x50, spec("/connectWiFi", Perm::SIGNED, "", {{128, "The WiFi did not connect"}}), "",
              "", "Connected"),
        typed(0x51, spec("/getIP", Perm::OPEN, "", {{128, "The node has no IP address"}}), "",
              "a:u b:u c:u d:u", "{0}.{1}.{2}.{3}"),
        typed(0x52,
              spec("/resetWiFiData", Perm::SIGNED, "",
                   {{128, "The WiFi settings could not be reset", "x"}}),
              "", "", "WiFi data reset successfully"),
        typed(0x78,
              spec("/install", Perm::SIGNED,
                   "[seconds] (default " + n + ", 0 stops, up to " +
                       std::to_string(INSTALL_MAX_SECONDS) + ")"),
              "seconds:u?", "on:u? off:e(off)?", "OK install [{0} s][{1}]"),
        typed(0x79, spec("/report", Perm::OPEN), "", "every:u src:e(default,stored) next:u then:u?",
              "report every {0} s ([install mode, then {3} s ]{1}), next in {2} s"),
        typed(0x7A,
              spec("/report.set", Perm::SIGNED,
                   "<seconds from " + std::to_string(REPORT_INTERVAL_MIN_S) + " to " +
                       std::to_string(REPORT_INTERVAL_MAX_S) + ", or 0 for the default>",
                   {{128, "The interval could not be stored", "x"}}),
              "seconds:u", "stored:u? def:u?", "Report interval set to [{0} s][default ({1} s)]"),
        typed(0x60, spec("/displayOn", Perm::OPEN, "", display), "", "", "Display On"),
        typed(0x61, spec("/displayOff", Perm::OPEN, "", display), "", "", "Display Off"),
        typed(0x62, spec("/displayBlink", Perm::OPEN, "", display), "", "", "Display Blink"),
        typed(0x63, spec("/displayClear", Perm::OPEN, "", display), "", "", "Display Clear"),
        typed(0x64, spec("/displayLogo", Perm::OPEN, "", display), "", "", "Display Logo"),
        typed(0x65, spec("/displayText", Perm::OPEN, "<text>", display), "text:s", "",
              "Display Text"),
    };
    for (const CommandSpec& s : specs) EXPECT_TRUE(catalogue.add(s)) << s.name;
    return catalogue;
}

/** @return the typed declaration of command @p name of the firmware catalogue, with its errors. */
inline TypedDeclaration firmwareDeclaration(const std::string& name) {
    const CommandCatalogue catalogue = firmwareCatalogue();
    const CommandSpec* found = catalogue.find(name);
    TypedDeclaration declaration;
    EXPECT_NE(found, nullptr) << name;
    if (found != nullptr) {
        EXPECT_TRUE(typedDeclarationOf(*found, declaration)) << name;
    }
    return declaration;
}
