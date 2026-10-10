import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import fault_matrix as fm  # noqa: E402

FLOOR = "I (28) BOOT part=app0 state=VALID ver=0.1.0+g1 bl=20f487 rr={rr}"
TEST = "I (30) BOOT part=app1 state=PENDING_VERIFY ver=0.1.1+g1 bl=20f487 rr=SW"


def crash_log(second_report=True):
    """Bench log of the crash image on a sensor (2026-10-10, COM6)."""
    attempt = ["I (40795) OtaInstall: Manifest for tbeam 0.1.1+g1, 1287712 B, key 1",
               "W (68658) OtaPull: Image OK after 1 attempt(s): 1287712 B in 26315 ms",
               "I (68882) OtaReport: Report written: HTTP 204",
               TEST,
               "W (53) OtaBootGuard: TEST_IMAGE crash: aborting",
               FLOOR.format(rr="PANIC")]
    lines = attempt + ["E (829) OtaBootGuard: Update attempt rolled back, reason 0, unexplained 1",
                       "I (3421) MaintWifi: Resuming the pull window, 267 s left",
                       "I (8507) OtaReport: Report rollback: HTTP 204"]
    lines += attempt + ["E (825) OtaBootGuard: Update attempt rolled back, reason 0, "
                        "unexplained 2, blacklisted"]
    if second_report:
        lines.append("I (8400) OtaReport: Report rollback: HTTP 204")
    lines += ["I (8571) OtaInstall: Not installing it: SKIP_BLACKLISTED",
              "I (8763) OtaReport: Report noop: HTTP 204"]
    return lines


def explained_log(reason, rr="SW"):
    return ["I (40795) OtaInstall: Manifest for tbeam 0.1.1+g1, 1288464 B, key 1",
            "W (68658) OtaPull: Image OK after 1 attempt(s): 1288464 B in 27749 ms",
            "I (68882) OtaReport: Report written: HTTP 204",
            TEST,
            f"E (2779) OtaBootGuard: Self-test failed, reason {reason}, rolling back",
            FLOOR.format(rr=rr),
            f"E (828) OtaBootGuard: Update attempt rolled back, reason {reason}, unexplained 0, "
            "blacklisted",
            "I (8374) OtaReport: Report rollback: HTTP 204",
            "I (8945) OtaInstall: Not installing it: SKIP_BLACKLISTED",
            "I (9058) OtaReport: Report noop: HTTP 204",
            "I (9100) MaintWifi: Window closed after 5824 ms"]


def events(lines):
    return [event for event in map(fm.parse_event, lines) if event is not None]


def test_parses_every_event_line():
    assert fm.parse_event(TEST) == ("boot", "app1", "PENDING_VERIFY", "SW")
    assert fm.parse_event("\x1b[0;31mE (825) OtaBootGuard: Update attempt rolled back, reason 0, "
                          "unexplained 2, blacklisted\x1b[0m") == ("rolled_back", 0, 2, True)
    assert fm.parse_event("E (829) OtaBootGuard: Update attempt rolled back, reason 6, "
                          "unexplained 0") == ("rolled_back", 6, 0, False)
    assert fm.parse_event("I (9) OtaInstall: Not installing it: SKIP_BLACKLISTED") == (
        "refused", "SKIP_BLACKLISTED")
    assert fm.parse_event("I (9) OtaReport: Report rollback: HTTP 204") == (
        "report", "rollback", 204)
    assert fm.parse_event("I (9) OtaReport: Report written: HTTP -1") == ("report", "written", -1)
    assert fm.parse_event("W (6) OtaPull: Image OK after 2 attempt(s): 12 B") == ("installed",)
    assert fm.parse_event("I (1) OtaInstall: Manifest for tbeam 0.1.1, 1 B, key 1") == (
        "manifest",)
    assert fm.parse_event("I (1) MaintWifi: Window closed after 5 ms") == ("window_closed",)
    assert fm.parse_event("[INFO] Superframe service started") is None


def test_crash_image_passes_with_two_reported_rollbacks():
    assert fm.judge(fm.KINDS[1], events(crash_log())) == []


def test_a_missing_rollback_report_fails():
    problems = fm.judge(fm.KINDS[1], events(crash_log(second_report=False)))
    assert problems == ["2 rollback reports expected, 1 accepted"]


def test_irq_hang_needs_the_interrupt_watchdog_reset():
    problems = fm.judge(fm.KINDS[2], events(crash_log()))
    assert problems == ["reset reason PANIC after the fault, expected INT_WDT",
                        "reset reason PANIC after the fault, expected INT_WDT"]


def test_selftest_fail_and_loop_hang_roll_back_once_with_their_reason():
    assert fm.judge(fm.KINDS[4], events(explained_log(5))) == []
    assert fm.judge(fm.KINDS[3], events(explained_log(6))) == []


def test_loop_hang_caught_by_the_rtc_watchdog_fails():
    lines = [line.replace("reason 6, unexplained 0, blacklisted", "reason 0, unexplained 1")
             for line in explained_log(6, rr="WDT") if "Self-test failed" not in line]
    problems = fm.judge(fm.KINDS[3], events(lines))
    assert "reset reason WDT after the fault, expected SW" in problems
    assert "rollback reason 0, expected 6" in problems
    assert "the last rollback did not blacklist the image" in problems


def test_an_image_that_is_never_refused_fails():
    lines = explained_log(5)[:8]
    assert "the image was never refused as blacklisted" in fm.judge(fm.KINDS[4], events(lines))


def test_server_reports_are_counted_per_node():
    reports = [{"node": "7680", "event": "written"}, {"node": "7680", "event": "rollback"},
               {"node": "7680", "event": "noop"}, {"node": "6D4C", "event": "written"}]
    assert fm.judge_server(fm.KINDS[4], reports, "7680") == []
    assert fm.judge_server(fm.KINDS[4], reports, "6D4C") == [
        "server: 1 rollback reports expected, 0 received", "server: no noop report"]


def test_kinds_cover_the_four_fault_images():
    assert sorted(fm.KINDS) == [1, 2, 3, 4]
    assert [fm.KINDS[k].name for k in (1, 2, 3, 4)] == [
        "crash", "irq-hang", "loop-hang", "selftest-fail"]


class FakeGateway:
    """A gateway: each /maint.open runs one update check, nothing more."""

    def __init__(self, checks):
        self.checks = list(checks)
        self.out = []
        self.written = []

    def write(self, data):
        self.written.append(data.decode().strip())
        if self.checks:
            self.out += [(line + "\n").encode() for line in self.checks.pop(0)]
        return len(data)

    def readline(self):
        return self.out.pop(0) if self.out else b""


def test_driver_opens_windows_until_the_image_is_refused(monkeypatch):
    monkeypatch.setattr(fm, "REOPEN_AFTER_S", 0.2)
    log = explained_log(5)
    gateway = FakeGateway([log[:8], log[8:10]])
    events = fm.drive(gateway, gateway=True, deadline=fm.time.monotonic() + 10, say=lambda _: None)
    assert gateway.written == ["/maint.open 60", "/maint.open 60"]
    assert fm.judge(fm.KINDS[4], events) == []


def test_driver_gives_up_at_the_deadline(monkeypatch):
    monkeypatch.setattr(fm, "STALL_S", 100)
    gateway = FakeGateway([["I (1) OtaInstall: Manifest for tbeam 0.1.1, 1 B, key 1"]])
    events = fm.drive(gateway, gateway=False, deadline=fm.time.monotonic() + 1, say=lambda _: None)
    assert gateway.written == ["/maint.open 900"]
    assert "the image was never refused as blacklisted" in fm.judge(fm.KINDS[4], events)
