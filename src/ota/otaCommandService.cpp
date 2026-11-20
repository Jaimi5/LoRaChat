#include "otaCommandService.h"

#include "commands/commandRouter.h"
#include "maintWifi.h"
#include "otaService.h"
#include "wifi/wifiServerService.h"

// The reply text of /maint.open names the limit of windows in 24 h.
static_assert(MaintWindowPlan::MAX_PER_DAY == 8, "update the reply template of /maint.open");

OtaCommandService::OtaCommandService() {
    addCommand(Command(0x30, "/key", "Show whether a deployment key is stored, and its fingerprint",
                       Perm::OPEN)
                   .reply("none:e(No)? fp:r?",
                          "[{0} deployment key][Deployment key stored, fingerprint {1}]")
                   .run([](const Fields&, Fields& out) {
                       return OtaService::getInstance().keyInto(out);
                   }));
    addCommand(Command("/key.set", "Store the deployment key", Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setKey(args); })
                   .withUsage("<64 hex digits>")
                   .withCode(OTA_ERR_FAILED, "The key could not be stored"));
    addCommand(
        Command(0x32, "/maint.open",
                "Open a maintenance window. Without ap the node joins its WiFi network and "
                "checks for updates. With ap it opens its own access point for an upload",
                Perm::SIGNED)
            .args("seconds:u ap:e(ap)?")
            .reply("left:u? pull:u? apfor:u? count:u? gw:e(gateway)? ssid:s?",
                   "[Window extended, {0} s left][Pull window open for {1} s][Access point {5} "
                   "open for {2} s, upload at http://192.168.4.1/][ ({3} of 8 in 24 h)][Checking "
                   "the OTA server over the {4} WiFi]")
            .run([](const Fields& in, Fields& out) {
                return OtaService::getInstance().openWindow(in.u(0), in.has(1), out);
            })
            .withUsage(String("<seconds> [ap] (up to ") + MaintWindowPlan::MAX_PULL_S + ", or " +
                       MaintWindowPlan::MAX_AP_S + " with ap)")
            .withCode(MAINT_ERR_GATEWAY_AP,
                      "Gateways keep their own WiFi. The access point is for sensors")
            .withCode(MAINT_ERR_VERIFYING, "The new image is still being verified. Try again later")
            .withCode(MAINT_ERR_NO_KEY, "The node has no deployment key")
            .withCode(MAINT_ERR_POLICY, "Refused, battery or memory too low",
                      "e(REFUSE_BATTERY,REFUSE_HEAP)")
            .withCode(MAINT_ERR_DAILY_LIMIT, "Too many windows in the last 24 h")
            .withCode(MAINT_ERR_STILL_OPEN, "The open window did not close")
            .withCode(MAINT_ERR_NO_TASK, "The window could not be started")
            .withCode(MAINT_ERR_GATEWAY_WIFI, "The gateway WiFi is not connected")
            .withCode(MAINT_ERR_CHECK_RUNNING, "An update check is already running"));
    addCommand(Command(0x33, "/maint.close", "Close the maintenance window", Perm::SIGNED)
                   .reply("none:e(No)? closing:e(Closing)?", "[{0} window open][{1} the window]")
                   .run([](const Fields&, Fields& out) {
                       return OtaService::getInstance().closeWindow(out);
                   }));
    // A text line also tells the address of the node, which the asker of a binary frame knows.
    addCommand(Command(0x34, "/version",
                       "Show version, slot, image state, bootloader hash, board and address",
                       Perm::OPEN)
                   .reply("ver:v slot:e(app0,app1)? state:e(VALID,PENDING_VERIFY) bl:h3 "
                          "env:e(tbeam,tbeam-debug)? git:h4 dirty:e(dirty)?",
                          "{0}+g{5}[.{6}][ {1}] {2} bl={3}[ env={4}]")
                   .run([](const Fields&, Fields& out) {
                       return OtaService::getInstance().versionInto(out);
                   })
                   .runText([](String args, const Command& self) {
                       CommandReply reply = self.runTyped(args);
                       if (!reply.ok()) return reply;
                       char node[sizeof(" node=FFFF")];
                       snprintf(node, sizeof(node), " node=%04X",
                                LoRaMeshService::getInstance().getLocalAddress());
                       reply.text += node;
                       return reply;
                   }));
    addCommand(
        Command(0x35, "/ota.status", "Show the last update attempt and its outcome", Perm::OPEN)
            .reply("last:e(NONE,VALID,ROLLED_BACK,BL_NO_ROLLBACK)? pending:e(no,yes) reason:u "
                   "unexplained:u blacklisted:u",
                   "last=[{0}] pending={1} reason={2} unexplained={3} blacklisted={4}")
            .run([](const Fields&, Fields& out) {
                return OtaService::getInstance().statusInto(out);
            }));
    addCommand(Command(0x36, "/reboot", "Restart the node", Perm::SIGNED)
                   .reply("", "Rebooting")
                   .run([](const Fields&, Fields&) { return OtaService::getInstance().reboot(); })
                   .withCode(OTA_ERR_FAILED, "The restart could not be scheduled"));
    addCommand(Command("/ota.server", "Show or set the OTA server", Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setServer(args); })
                   .withUsage("[http://host:port/folder/ or default]")
                   .withCode(OTA_ERR_FAILED, "The server could not be stored"));
    // The text handler stays for the serial console, where the credentials may be typed in
    // the clear. A signed line carries them sealed, as the typed argument does.
    addCommand(Command(0x38, "/maint.wifi", "Store the WiFi credentials", Perm::SIGNED)
                   .args("sealed:r")
                   .reply("re:e(reconnecting)? ssid:s", "WiFi credentials stored for {1}[, {0}]")
                   .run([](const Fields& in, Fields& out) {
                       return OtaService::getInstance().setWifiSealed(in.bytes(0), out);
                   })
                   .runText([](String args, const Command& self) {
                       uint32_t counter = 0;
                       if (CommandRouter::getInstance().runningCounter(counter)) {
                           return self.runTyped(args);
                       }
                       Fields out;
                       const TypedResult stored = OtaService::getInstance().setWifiPlain(args, out);
                       return self.render(stored, out);
                   })
                   .withUsage("<credentials encrypted by lmcmd.py wifi>, or on serial <ssid> "
                              "[<password of 8 to 63 characters>]")
                   .withCode(WIFI_ERR_INVALID, "The WiFi credentials are not valid")
                   .withCode(WIFI_ERR_WINDOW_OPEN, "A maintenance window is open. Close it first")
                   .withCode(WIFI_ERR_STORE, "The WiFi credentials could not be stored", "x"));
}
