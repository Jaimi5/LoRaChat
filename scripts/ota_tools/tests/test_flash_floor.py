import csv
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ap_pass  # noqa: E402
import flash_floor as ff  # noqa: E402

KEY = bytes(range(32))


class FakeNode:
    """Serial port of a node running the floor image."""

    def __init__(self, address=0x7680, fingerprint=None, boots=True, pmu="AXP2101",
                 routes=True, report="HTTP 204"):
        self.address = address
        self.fingerprint = fingerprint or ap_pass.key_fingerprint(KEY)
        self.out = []
        self.written = []
        self.boots = boots
        self.pmu = pmu
        self.routes = routes
        self.report = report
        self.rts = False
        self.dtr = False

    def boot_line(self):
        return b"I (45) BOOT part=app0 state=VALID ver=0.2.0+gabc1234 bl=20f487 rr=POWERON\n"

    def reset(self):
        if self.boots:
            self.out += [b"rst:0x1 (POWERON_RESET)\n", self.boot_line()]

    def write(self, data):
        line = data.decode().strip()
        self.written.append(line)
        name = line.split(" ")[0]
        replies = {
            "/version": f"0.2.0+gabc1234 app0 VALID bl=20f487 env=tbeam node={self.address:04X}",
            "/key.set": f"Deployment key stored, fingerprint {self.fingerprint}",
            "/key": f"Deployment key stored, fingerprint {self.fingerprint}",
            "/maint.wifi": "WiFi credentials stored for FarmNet",
            "/role.set": "Role set to sensor, restarting",
            "/power": f"pmu={self.pmu} vbus=yes battery=no 0mV -1% charging=no"
                      if self.pmu else "pmu=none",
            "/ota.server": "OTA server: http://example/fw/tbeam/",
            "/maint.open": "Pull window open for 90 s (1 of 8 in 24 h)",
            "/identify": "Blinking for 120 s",
            "/getRT": "--- Routing Table ---",
        }
        self.out += [f"> {name}\n".encode(), (replies.get(name, "Unknown") + "\n").encode()]
        if name == "/role.set":
            self.out += [b"rst:0xc (SW_CPU_RESET)\n", self.boot_line()]
        if name == "/getRT":
            self.out.append(b"5E9C (hops:1 lq:200) - Via: 5E9C\n" if self.routes
                            else b"No routes\n")
        if name == "/maint.open":
            self.out += [b"I (9000) MaintWifi: Joined FarmNet in 4000 ms, RSSI -60 dBm\n",
                         f"I (9300) OtaReport: Report noop: {self.report}\n".encode()]
        return len(data)

    def readline(self):
        return self.out.pop(0) if self.out else b""


def test_esptool_arguments_flash_everything_and_erase_nvs():
    images = ff.ReleaseImages.from_folder(ff.Path("REL"))
    args = ff.esptool_args("COM6", images, erase_nvs=True)
    assert args[:4] == ["--chip", "esp32", "--port", "COM6"]
    assert "--after" in args and args[args.index("--after") + 1] == "no_reset"
    flash = args[args.index("write_flash") + 1:]
    assert flash == ["0x1000", str(ff.Path("REL") / "bootloader.bin"),
                     "0x8000", str(ff.Path("REL") / "partitions.bin"),
                     "0x9000", str(images.blank_nvs),
                     "0xe000", str(ff.Path("REL") / "ota_data_initial.bin"),
                     "0x10000", str(ff.Path("REL") / "firmware.bin")]
    kept = ff.esptool_args("COM6", images, erase_nvs=False)
    assert "0x9000" not in kept


def test_parses_mac_from_esptool_output():
    output = "Chip is ESP32-D0WDQ6-V3 (revision v3.1)\nMAC: 34:98:7a:6d:76:80\nUploading stub..."
    assert ff.parse_mac(output) == "34:98:7a:6d:76:80"
    with pytest.raises(ff.FloorError):
        ff.parse_mac("no mac here")


def test_parses_the_boot_line_and_version():
    boot = ff.parse_boot("I (45) BOOT part=app0 state=VALID ver=0.2.0+g1 bl=20f487 rr=POWERON")
    assert boot == {"part": "app0", "state": "VALID", "ver": "0.2.0+g1", "bl": "20f487",
                    "rr": "POWERON"}
    assert ff.parse_node("0.2.0 app0 VALID bl=20f487 env=tbeam node=7680") == 0x7680


def test_provisions_a_node_and_writes_the_inventory(tmp_path):
    node = FakeNode()
    row = ff.provision(node, KEY, wifi=("FarmNet", "password123"), role="sensor", timeout=0.5)
    assert node.written[0] == "/version"
    assert node.written[1] == "/key.set " + KEY.hex()
    assert node.written[2] == "/maint.wifi FarmNet password123"
    assert node.written[3] == "/role.set sensor"
    assert row["node"] == "7680" and row["ver"] == "0.2.0+gabc1234" and row["bl"] == "20f487"
    assert row["ap_ssid"] == "LM-7680"
    assert row["wifi_qr"] == ap_pass.wifi_qr(KEY, 0x7680)
    assert row["key"] == ap_pass.key_fingerprint(KEY)

    inventory = tmp_path / "nodes.csv"
    row["mac"] = "34:98:7a:6d:76:80"
    ff.append_inventory(inventory, row)
    ff.append_inventory(inventory, dict(row, node="1A2B"))
    rows = list(csv.DictReader(inventory.open()))
    assert [r["node"] for r in rows] == ["7680", "1A2B"]
    assert rows[0]["mac"] == "34:98:7a:6d:76:80"


def test_refuses_a_node_that_stores_another_key():
    node = FakeNode(fingerprint="deadbeef")
    with pytest.raises(ff.FloorError, match="fingerprint"):
        ff.provision(node, KEY, timeout=0.5)


def test_reports_a_node_that_does_not_boot():
    with pytest.raises(ff.FloorError, match="BOOT"):
        ff.wait_for_boot(FakeNode(boots=False), timeout=0.3)


def test_parses_a_port_list():
    assert ff.parse_ports("COM6") == ["COM6"]
    assert ff.parse_ports("COM6, COM7,COM9") == ["COM6", "COM7", "COM9"]
    for bad in ("", ",", "COM6,COM6"):
        with pytest.raises(ff.FloorError):
            ff.parse_ports(bad)


def test_parses_the_power_chip():
    assert ff.parse_power("pmu=AXP192 vbus=yes battery=no") == "AXP192"
    assert ff.parse_power("pmu=none") == "none"
    assert ff.parse_power("vbus=yes") == "none"


def test_waits_for_the_restart_after_the_role_is_set():
    node = FakeNode()
    ff.provision(node, KEY, role="sensor", timeout=0.5)
    assert ff.wait_for_restart(node, timeout=0.5)["state"] == "VALID"


def test_a_good_node_passes_every_check():
    node = FakeNode()
    boot = {"state": "VALID", "bl": "20f487"}
    results = ff.run_checks(node, boot, "20f487", ap_pass.key_fingerprint(KEY),
                            "http://example/fw/tbeam/", mesh_timeout=1, wifi_timeout=1)
    assert ff.summarize(results) == ("boot=ok bl=ok pmu=ok key=ok mesh=ok wifi=ok", "PASS")
    assert results["pmu_model"] == "AXP2101"
    assert "/ota.server http://example/fw/tbeam/" in node.written
    assert "/maint.open 90" in node.written


def test_each_fault_shows_in_its_check():
    node = FakeNode(pmu=None, routes=False, report="HTTP 401", fingerprint="deadbeef")
    boot = {"state": "PENDING_VERIFY", "bl": "aaaaaa"}
    results = ff.run_checks(node, boot, "20f487", ap_pass.key_fingerprint(KEY), None,
                            mesh_timeout=0.3, wifi_timeout=1)
    text, verdict = ff.summarize(results)
    assert verdict == "FAIL"
    assert text == ("boot=PENDING_VERIFY bl=aaaaaa pmu=none key=wrong key mesh=no neighbour "
                    "wifi=HTTP 401")


def test_reads_the_expected_bootloader_from_the_release(tmp_path):
    (tmp_path / "info.json").write_text('{"bootloader_sha256": "20f487dd7cf9"}')
    images = ff.ReleaseImages.from_folder(tmp_path)
    assert images.expected_bootloader() == "20f487"
    assert ff.ReleaseImages.from_folder(tmp_path / "missing").expected_bootloader() == ""


def test_prints_one_table_row_per_port():
    rows = [{"port": "COM6", "mac": "34:98:7a:6d:76:80", "node": "7680", "ver": "0.1.2",
             "pmu": "AXP2101", "result": "PASS", "checks": "boot=ok"},
            {"port": "COM8", "result": "ERROR", "checks": "esptool failed"}]
    table = ff.format_table(rows).splitlines()
    assert table[0].split() == ["port", "mac", "node", "ver", "pmu", "result", "checks"]
    assert table[1].split()[:6] == ["COM6", "34:98:7a:6d:76:80", "7680", "0.1.2", "AXP2101",
                                    "PASS"]
    assert table[2].startswith("COM8") and "ERROR" in table[2]
