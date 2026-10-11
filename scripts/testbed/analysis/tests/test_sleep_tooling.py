"""Tests for the member-sleep tooling on synthetic logs.

    python3 -m pytest scripts/testbed/analysis/tests -q
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

TESTBED = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TESTBED))
sys.path.insert(0, str(TESTBED / "lm200"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from fixtures import app, lib, lifecycle, make_run, slot  # noqa: E402

from analysis import energy_model, formation, lm200_metrics, sleep_metrics  # noqa: E402
from analysis.exclusions import excluded_reason  # noqa: E402
from analysis.parse_logs import parse_run  # noqa: E402
from runner import run_checks  # noqa: E402

M = "4B34"  # CB34's runtime address (bit 15 cleared)


def member_log() -> list[tuple[float, str]]:
    L = [
        (0.0, "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)"),
        (1.0, app(f"CFG node=0x{M} sf=9 pdr=1 sleep=2 msf=0.50 dds=1 mgr=0x006C pc=9 pd=240000 ss=0 rel=0 grp=0")),
        (5.0, lib(M, "Network service state changed to 1")),
        (40.0, lib(M, "Network service state changed to 3")),
        (50.0, app(f"HEALTH node=0x{M} heap=150000 minheap=140000 role=1 state=3 "
                   "lsl_n=0 lsl_ms=0 dsl_n=0 dsl_ms=0 boot_reason=1")),
        # sleep 1: announced, woken, resumed, confirmed
        (100.0, app(f"SLEEPCB node=0x{M} type=deep planned_ms=20000")),
        (100.1, lib(M, "Deep sleep for 20000 ms (state 300 bytes, waking 50 ms early)")),
        (120.0, "rst:0x5 (DEEPSLEEP_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)"),
        (120.4, app(f"BOOT node=0x{M} reason=8 app_ms=200")),
        (120.5, app(f"BOOT_READY node=0x{M} app_ms=350")),
        (120.6, lib(M, "Network service state changed to 3")),
        (120.6, lib(M, "Resumed membership of network 0x006C: manager 0x006C, control slot 3, 12 routes")),
        (120.7, lib(M, "Resuming superframe #12 at slot 5 (start 1234 ms)")),
        (120.7, lib(M, "Resumed network 0x006C after 19950 ms of deep sleep")),
        (125.0, lib(M, "Sleep clock: 30 ms off after 19950 ms of sleep, error now 1500 ppm (3 samples)")),
        (125.1, lib(M, "Schedule confirmed by a sync beacon after the resume")),
        (150.0, app(f"HEALTH node=0x{M} heap=150000 minheap=140000 role=1 state=3 "
                    "lsl_n=4 lsl_ms=8000 dsl_n=1 dsl_ms=20000 boot_reason=8")),
        (160.0, lib(M, "No deep sleep: messages queued", "DEBUG")),
        (161.0, lib(M, "No deep sleep: messages queued", "DEBUG")),
        (170.0, app(f"APP_TX_LATE src=0x{M} seq=3 late_ms=1500")),
        # sleep 2: woken, cannot resume, cold start, rejoin
        (200.0, lib(M, "Deep sleep for 20000 ms (state 300 bytes, waking 50 ms early)")),
        (220.2, "rst:0x5 (DEEPSLEEP_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)"),
        (220.7, app(f"BOOT_READY node=0x{M} app_ms=360")),
        (220.8, lib(M, "Cannot resume: the persistent clock restarted", "WARNING")),
        (221.0, lib(M, "Network service state changed to 1")),
        (230.0, lib(M, "Join request scheduled in discovery slot 4", "DEBUG")),
        (260.0, lib(M, "Network service state changed to 3")),
        # an rst:0x5 nobody announced -> unexpected reboot
        (300.0, "rst:0x5 (DEEPSLEEP_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)"),
        (310.0, app(f"HEALTH node=0x{M} heap=150000 minheap=140000 role=1 state=3 "
                    "lsl_n=1 lsl_ms=1000 dsl_n=0 dsl_ms=0 boot_reason=8")),
    ]
    return L


def nm_log() -> list[tuple[float, str]]:
    L = [(0.0, "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)"),
         (2.0, lib("006C", "Network service state changed to 4"))]
    L += [(t, app("HEALTH node=0x006C heap=150000 minheap=140000 role=4 state=4")) for t in range(30, 331, 60)]
    return L


def run_with(tmp_path, member=None, nm=None, **kw):
    return make_run(tmp_path, {"CB34": member or member_log(), "006C": nm or nm_log()},
                    defaults={"sim_sleep_mode": 2, "lora_min_sleep_fraction": 0.5, "sim_pdr_compare": 1,
                              "packet_count": 9, "packet_delay": 240000, **kw.pop("defaults", {})}, **kw)


# ── parse_logs ───────────────────────────────────────────────────────────────

def test_deep_wake_expected_and_resume_tagged(tmp_path):
    ev = parse_run(run_with(tmp_path), window=(None, None))
    resets = [e for e in ev if e.kind == "crash" and e.fields["kind"] == "reset" and e.node == "CB34"]
    assert [r.fields["deep_sleep_wake"] for r in resets] == [False, True, True, False]
    assert [r.fields["expected"] for r in resets] == [True, True, True, False]
    sc3 = [e for e in ev if e.kind == "state_change" and e.node == "CB34" and e.fields["state"] == 3]
    assert [bool(e.fields.get("resume")) for e in sc3] == [False, True, False]
    h = [e for e in ev if e.kind == "health" and e.node == "CB34"]
    assert h[1].fields["lsl_ms"] == 8000 and h[1].fields["boot_reason"] == 8
    cfg = next(e for e in ev if e.kind == "cfg")
    assert cfg.fields["msf"] == "0.50" and cfg.fields["mgr"] == "0x006C"
    cr = next(e for e in ev if e.kind == "cannot_resume")
    assert cr.fields["category"] == "clock_restarted"


def test_late_rst5_is_not_a_wake(tmp_path):
    lines = [(0.0, "rst:0x1 (POWERON_RESET)"),
             (10.0, lib(M, "Deep sleep for 5000 ms (state 300 bytes, waking 50 ms early)")),
             (30.0, "rst:0x5 (DEEPSLEEP_RESET)")]       # 20 s after a 5 s sleep: > 5 + 10 s slack
    ev = parse_run(make_run(tmp_path, {"CB34": lines}), window=(None, None))
    r = [e for e in ev if e.kind == "crash"][-1]
    assert not r.fields["deep_sleep_wake"] and not r.fields["expected"]


# ── sleep_metrics / lm200_metrics ────────────────────────────────────────────

def test_sleep_stats(tmp_path):
    run = run_with(tmp_path)
    ev = parse_run(run, window=(None, None))
    st = sleep_metrics.sleep_stats(ev, superframe_s=30.0, members={"CB34"})
    assert st["present"] and st["n_deep_sleeps"] == 2 and st["n_wakes"] == 2
    assert st["n_resumed"] == 1 and st["resume_success"] == 0.5
    assert st["n_cannot_resume"] == 1 and st["cannot_resume_by_reason"] == {"clock_restarted": 1}
    assert st["cold_starts_after_wake"] == 1
    assert st["n_unannounced_deepsleep_resets"] == 1
    assert st["frac_members_deep_slept"] == 1.0
    assert st["boot_ms"]["n"] == 2 and st["boot_ms"]["p50"] == pytest.approx(500.0, abs=1)
    assert st["time_to_ready_ms"]["p95"] == pytest.approx(5100.0, abs=1)
    assert st["time_to_ready_ms"]["p95_superframes"] == pytest.approx(0.17, abs=0.01)
    assert st["sleep_clock"]["abs_residual_ppm"]["p50"] == pytest.approx(30 / 19950 * 1e6, rel=1e-3)
    assert st["rejoined_members"] == ["CB34"] and st["join_attempts_after_resume"] == 1
    # light sleep: 0 -> 4/8000 -> (counter restarted) 1/1000
    assert st["light_sleep"]["n_total"] == 5 and st["light_sleep"]["ms_total"] == 9000
    assert st["no_deep_sleep_reasons"] == {"messages queued": 2}
    assert st["app_tx_late"]["n"] == 1 and st["app_tx_late"]["per_node"] == {"CB34": 1}
    assert st["per_node"]["CB34"]["app_tx_late"] == 1
    assert st["per_node"]["CB34"]["deep_ms"] == pytest.approx(40100, abs=5)


def test_crash_and_hang_stats(tmp_path):
    run = run_with(tmp_path)
    m = lm200_metrics.compute_all(run)
    assert m["crashes"]["unexpected_resets"] == {"CB34": 1}
    assert m["crashes"]["deep_sleep_wakes_total"] == 2
    assert m["sleep"]["members"] == ["CB34"]       # 006C is the configured manager
    assert m["health"]["suspected_hangs"] == []


def test_deep_sleep_counts_as_liveness(tmp_path):
    # Last HEALTH at 50 s, then a 600 s deep sleep running past the end of the logs (400 s).
    member = [(0.0, "rst:0x1 (POWERON_RESET)"),
              (50.0, app(f"HEALTH node=0x{M} heap=1 minheap=1 role=1 state=3")),
              (60.0, lib(M, "Deep sleep for 600000 ms (state 300 bytes, waking 50 ms early)"))]
    nm = [(t, app("HEALTH node=0x006C heap=1 minheap=1 role=4 state=4")) for t in range(0, 401, 50)]
    ev = parse_run(make_run(tmp_path, {"CB34": member, "006C": nm}), window=(None, None))
    assert lm200_metrics.health_stats(ev)["suspected_hangs"] == []
    no_sleep = [e for e in ev if e.kind != "deep_sleep"]
    assert lm200_metrics.health_stats(no_sleep)["suspected_hangs"] == ["CB34"]


# ── formation ────────────────────────────────────────────────────────────────

def test_resume_is_not_a_join_or_flap(tmp_path):
    run = run_with(tmp_path, devices={"CB34": {}, "006C": {}})
    f = formation.analyse(run, window="run")
    # Joins: first state 3 at 40 s; the resume at 120.6 s is neither a flap nor a rejoin,
    # the cold start after "Cannot resume" (221 s -> 260 s) and the unannounced reset are.
    assert f["joined_at"]["CB34"] is not None
    assert f["flaps"].get("CB34") == 2 and f["rejoins"].get("CB34") == 1
    assert f["first_join_since_reset_s"] == pytest.approx(32.0, abs=0.01)   # NM at 2 s, reset at -30 s


# ── energy model ─────────────────────────────────────────────────────────────

def test_energy_model_deep_light_split(tmp_path):
    # 1000 s window: SLEEP slot 100-900 s containing a 500 s deep sleep (wake at 600.5 s,
    # BOOT_READY 0.5 s later), HEALTH says 200 s of light sleep in the window.
    member = [(0.0, slot(0, "RX")), (100.0, slot(1, "SLEEP")), (900.0, slot(2, "TX")), (1000.0, slot(3, "RX")),
              (50.0, app(f"HEALTH node=0x{M} heap=1 minheap=1 role=1 state=3 lsl_n=0 lsl_ms=0 dsl_n=0 dsl_ms=0 boot_reason=1")),
              (100.5, lib(M, "Deep sleep for 500000 ms (state 300 bytes, waking 50 ms early)")),
              (600.5, "rst:0x5 (DEEPSLEEP_RESET)"),
              (601.0, app(f"BOOT_READY node=0x{M} app_ms=300")),
              (950.0, app(f"HEALTH node=0x{M} heap=1 minheap=1 role=1 state=3 lsl_n=10 lsl_ms=200000 dsl_n=1 dsl_ms=500000 boot_reason=8"))]
    run = make_run(tmp_path, {"CB34": member}, phases=lifecycle(meas_s=(0.0, 1000.0)))
    params = energy_model.load_params()
    res = energy_model.analyse(run, params)
    n = res["per_node"]["CB34"]
    assert res["label"] == "modelled"
    assert n["mcu_ms"]["deep"] == pytest.approx(500000, abs=1)
    assert n["mcu_ms"]["boot"] == pytest.approx(500, abs=1)
    assert n["mcu_ms"]["light"] == pytest.approx(200000, abs=1)
    assert n["mcu_ms"]["active"] == pytest.approx(1000000 - 500000 - 500 - 200000, abs=1)
    mc = params["mcu"]
    want_mcu = (mc["active_mA"] * 299.5 + mc["deep_mA"] * 500 + mc["light_mA"] * 200 + mc["boot_mA"] * 0.5) / 1000
    assert n["mcu_mean_current_mA"] == pytest.approx(want_mcu, rel=1e-6)
    assert n["sleep_slot_split"]["awake_ms"] == pytest.approx(800000 - 500000 - 200000, abs=1)
    floor = energy_model.board_floor_mA(params)
    assert n["board"]["mean_current_mA"] - n["chip"]["mean_current_mA"] == pytest.approx(floor, rel=1e-6)


# ── run validity checks ──────────────────────────────────────────────────────

def test_suspend_and_window_checks():
    good = lifecycle(warmup_s=900, meas_s=(0, 1800), cooldown_s=120,
                     clocks=({"monotonic": 100.0, "boottime": 200.0, "wall": 1000.0},
                             {"monotonic": 3100.0, "boottime": 3200.0, "wall": 4000.0}))
    assert run_checks.suspend_check(good)[0] is True
    assert run_checks.window_check(good)[0] is True
    slept = [dict(p) for p in good]
    slept[-1].update(boottime=3200.0 + 500, wall=4000.0 + 500)
    ok, det = run_checks.suspend_check(slept)
    assert ok is False and "500" in det
    stretched = [dict(p) for p in good]
    for p in stretched:
        if p["name"] == "cooldown":
            p["end"] = p["end"].replace("T10:", "T19:")      # +9 h
    assert run_checks.window_check(stretched)[0] is False
    assert run_checks.suspend_check(lifecycle())[0] is None   # runner without clock marks


def test_log_gap_check(tmp_path):
    a = [(t, "x") for t in range(0, 600, 10)]
    b = [(t, "y") for t in list(range(0, 100, 10)) + list(range(500, 600, 10))]
    run = make_run(tmp_path, {"CB34": a, "006C": b})
    assert run_checks.log_gap_check(run)[0] is True            # CB34 kept logging
    run2 = make_run(tmp_path, {"CB34": b, "006C": b}, name="test-20261010-100000-x-r02")
    ok, det = run_checks.log_gap_check(run2)
    assert ok is False and "410 s" in det   # 90 s -> 500 s


CONFIG_H = """
#define SIM_PDR_COMPARE 0
#define LORA_MANAGER_ID 0x006C
#define LORA_SPREADING_FACTOR 9U
#define LORA_MIN_SLEEP_FRACTION 0
#define LORA_DEFAULT_DATA_SLOTS 1
#define PACKET_COUNT 200
#define PACKET_DELAY 120000
#define SIM_RELIABLE 0
#define SIM_GROUP 0
#define SIM_STOPSTART 0
#define SIM_SLEEP_MODE 0
"""


def test_config_leak_check(tmp_path):
    run = run_with(tmp_path, devices={"CB34": {}, "006C": {"node_active": 0}})
    ok, det = run_checks.config_leak_check(run, CONFIG_H)
    assert ok is True, det
    # Same log against a config.yaml that asked for SF7 and no sleep: leak.
    run2 = run_with(tmp_path, devices={"CB34": {"lora_spreading_factor": 7}, "006C": {"node_active": 0}},
                    defaults={"sim_sleep_mode": 0}, name="test-20261010-100000-y-r01")
    ok, det = run_checks.config_leak_check(run2, CONFIG_H)
    assert ok is False and "sf=9 (want 7)" in det and "sleep=2 (want 0)" in det
    # send= (SIM_SEND_PATH): compared when printed, absent on older firmware (above: no send field).
    cfg_send = app(f"CFG node=0x{M} sf=9 pdr=1 sleep=2 msf=0.50 dds=1 mgr=0x006C pc=9 pd=240000 ss=0 rel=0 grp=0 send=1")
    run4 = make_run(tmp_path, {"CB34": [(0.0, "rst:0x1"), (1.0, cfg_send)]}, name="test-20261010-100000-s-r01",
                    defaults={"sim_sleep_mode": 2, "lora_min_sleep_fraction": 0.5, "sim_pdr_compare": 1,
                              "packet_count": 9, "packet_delay": 240000, "sim_send_path": 1})
    assert run_checks.config_leak_check(run4, CONFIG_H)[0] is True
    run5 = make_run(tmp_path, {"CB34": [(0.0, "rst:0x1"), (1.0, cfg_send)]}, name="test-20261010-100000-t-r01",
                    defaults={"sim_sleep_mode": 2, "lora_min_sleep_fraction": 0.5, "sim_pdr_compare": 1,
                              "packet_count": 9, "packet_delay": 240000})
    ok, det = run_checks.config_leak_check(run5, CONFIG_H)   # config.h has no SIM_SEND_PATH: default 0
    assert ok is False and "send=1 (want 0)" in det
    # No CFG line anywhere: skipped.
    run3 = make_run(tmp_path, {"CB34": [(0.0, "rst:0x1")]}, name="test-20261010-100000-z-r01")
    assert run_checks.config_leak_check(run3, CONFIG_H)[0] is None


# ── gates: raw scan ──────────────────────────────────────────────────────────

def test_gates_scan_deep_wakes(tmp_path):
    import gates
    run = run_with(tmp_path)
    d = gates.scan_device_logs(run)["CB34"]
    assert d["deep_sleep_wakes"] == 2 and d["unexpected_reboots"] == 1


def test_sleep_gate_on_synthetic_runs(tmp_path):
    import gates
    root = tmp_path / "runs"
    run_with(root, batch="lm200s_sleep__slp", name="lm200s_sleep__slp-20261010-100000-sleep_deep_SF9-r01")
    cfg = {"thresholds": {"sleep": {"boot_time_ms": 1000}}}
    res = gates.sleep_gate([root / "lm200s_sleep__slp"], cfg, tmp_path / "cache")
    by = {c["name"]: c for c in res["checks"]}
    assert by["sleep:deep:members_deep_slept"]["status"] == "PASS"
    assert by["sleep:deep:resume_success"]["status"] == "FAIL"           # 1 of 2
    assert by["sleep:deep:rejoins_after_resume"]["status"] == "FAIL"
    assert by["sleep:deep:boot_ms_p95"]["status"] == "PASS"
    assert by["reboots"]["status"] == "FAIL"                             # the unannounced rst:0x5
    assert by["sleep:energy_delta_modelled"]["status"] == "PASS"
    assert by["sleep:deep:app_tx_late"]["value"] == 1
    assert res["energy_modelled"]["deep"]["app_tx_late_n"] == 1


# ── exclusions / configtool ──────────────────────────────────────────────────

def test_exclusions(tmp_path):
    run = make_run(tmp_path, {"CB34": [(0.0, "x")]}, name="b-20261010-100000-c-r01")
    assert excluded_reason(run) is None
    (tmp_path / "EXCLUDED.txt").write_text("# header\nb-20261010-100000-c-r01  # host suspended\n")
    assert excluded_reason(run) == "host suspended"


def test_generated_script_resets_config_h(tmp_path):
    repo = tmp_path / "repo"
    (repo / "src").mkdir(parents=True)
    (repo / "src" / "config.h").write_text("#define LORA_SPREADING_FACTOR 9U\n#define SIM_SLEEP_MODE 0\n")
    env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@t"}
    for cmd in (["git", "init", "-q"], ["git", "add", "."], ["git", "commit", "-qm", "init"]):
        subprocess.run(cmd, cwd=repo, check=True, env=env)
    # A previous board's edit that this experiment does not set:
    (repo / "src" / "config.h").write_text("#define LORA_SPREADING_FACTOR 9U\n#define SIM_SLEEP_MODE 2\n")
    exp = tmp_path / "exp.yaml"
    exp.write_text("experiment: t\ndefaults:\n  lora_spreading_factor: 12\ndevices:\n  CB34: {}\n")
    sys.path.insert(0, str(TESTBED / "configtool"))
    from generate import generate
    out = generate(exp, tmp_path / "cc")
    subprocess.run(["bash", str(out[0])], cwd=repo, check=True)
    text = (repo / "src" / "config.h").read_text()
    assert "#define SIM_SLEEP_MODE 0" in text and "#define LORA_SPREADING_FACTOR 12U" in text


def test_dds_verdict():
    import gates

    def s(ack, fail):
        return {"metrics": {"reliable": {"n_ack": ack, "n_fail": fail, "fail_reasons": {}}}}
    cfg = {}
    v = gates._dds_verdicts({"reliable_SF9_dds2": [s(99, 1), s(97, 3), s(96, 4)]}, cfg)
    assert v[0]["status"] == "PASS"
    v = gates._dds_verdicts({"reliable_SF9_dds2": [s(99, 1), s(70, 30)]}, cfg)
    assert v[0]["status"] == "FAIL"
    v = gates._dds_verdicts({"reliable_SF9_dds2": [s(99, 1), s(90, 10)]}, cfg)
    assert v[0]["status"] == "GREY"
    assert gates._dds_verdicts({"reliable_SF9_dds2": [s(99, 1)]}, cfg) == []   # one rep: no verdict
    assert json.dumps(v)
