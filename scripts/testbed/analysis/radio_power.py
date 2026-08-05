"""Per-state radio currents for the SX1276, keyed on configured TX power.

Single source of truth for the energy model. `duty_cycle.py` (measured slot
integration) and `model.py` (analytical superframe) both build their tables
here, so the two can never drift. Imports nothing from `analysis.*` — it is a
leaf, and `duty_cycle.py` already imports `model.py`.

Every constant below carries its provenance inline. If you change one, change
the citation with it: docs/paper/SECTION_7_7_HANDOFF.md quotes these values.

Why TX power matters
--------------------
The model used to hard-code I_TX = 120 mA regardless of `lora_power`. That is
the datasheet's *+20 dBm* figure, and it is measured with `RegPaDac = 0x87` —
the high-power PA bias mode. The testbed runs 2 dBm (dense cluster) and 14 dBm
(long-link ends) in `PaDac = 0x84`, so it never enters that mode at all. Since
v2 spends ~8% of its time transmitting against v1's ~0.7%, an inflated I_TX
penalises v2 specifically — the error was not symmetric.

RFO vs PA_BOOST
---------------
The datasheet's low-current TX rows (20 mA @ +7 dBm, 29 mA @ +13 dBm) are the
RFO pin. This hardware does not use it: RadioLib's SX1278::setOutputPower
defaults `forceRfo=False` and only auto-selects RFO below +2 dBm
(.pio/libdeps/*/RadioLib/src/modules/SX127x/SX1278.cpp:322), and neither src/,
LoRaMesher v1, nor v2 ever passes the flag. At `lora_power: 2` we stay on
PA_BOOST by exactly one dB. Measured, PA_BOOST costs ~3.2x RFO at 2 dBm
(46.7 vs 14.4 mA) — reading the RFO rows would understate v2's TX cost badly.

Sources
-------
[DS]    SX1276/77/78/79 datasheet Rev. 7 (May 2020), Table 6 p.14 (IDDT/IDDR/
        IDDST) and Table 32/34 (PaSelect, RegPaDac high-power mode).
[SOLER] Soler-Fernandez, Romera, Dieguez, Prades, Alonso, "LoRa Power Model for
        Energy Optimization in IoT Applications", Sensors 2026, 26(1), 301.
        doi:10.3390/s26010301. SX1276 on an SX1276MB1LAS shield, 3.3 V, Agilent
        B2912A SMU, 10 us sampling, 1000 reps/point. The only source found that
        states the PA path explicitly, characterising RFO (0-14 dBm) and
        PA_BOOST (2-20 dBm) separately, in the same PaDac=0x84 mode we deploy.
        TX/RX/SLEEP values below come from the authors' published dataset
        (github.com/jlusoler/LoRa_Power_Consumption_Simulator), not from a table
        in the paper — the paper reports them only as figures.

Known limits of [SOLER] — state these in any write-up, do not hide them:
  - Measured at 433 MHz (DS bands 2&3). We deploy 869.9 MHz (DS band 1). Their
    RX (12.27 mA) matches the bands-2&3 datasheet row (12.0 mA) to +2.3%, which
    both confirms genuine 433 MHz operation and pins shield overhead at ~0.3 mA.
  - On the SX1276MB1LAS, PA_BOOST is the 915 MHz (HF) path. Driving it at
    433 MHz is a load mismatch, which plausibly explains why their curve sits
    ~40 mA above the datasheet at 17 dBm (~127 vs 87). This weakens the absolute
    PA_BOOST numbers for our 868 MHz deployment. (Inference from Semtech's shield
    spec + their RX/band agreement; the paper does not address it.)
  - The source disagrees with itself on RX: body text says 13.2 mA / 350 ms, the
    dataset says 12.272 +/- 0.138 mA / 600 ms. Unexplained.
  - They ran minimum SF with CRC disabled; we run SF9 CR 4/7.

Do NOT use, having been checked and rejected:
  - Bouguera et al., Sensors 2018, 18(7), 2104, Table 3 — SX1272 and
    datasheet-derived rather than measured, and it lists 7 dBm above 13 dBm,
    which is physically impossible.
  - Piyare et al., Sensors 2018, 18(11), 3718 — its 50 mW "listening" is
    whole-mote (MSP430 + wake-up receiver + PMU + sensors), not radio-only.
  - I_RX = 14 mA — traces only to Casals et al. (Sensors 2017, 17(10), 2364)
    Table 1 quoting Magno et al. (DATE 2017), which is unverifiable at source.
    Casals' "min 46 mA (0 dBm)" is useful corroboration of the PA_BOOST floor,
    but it is second-hand and must not be a primary citation.
"""

from __future__ import annotations

# ── Deployment / firmware facts ──────────────────────────────────────────────
DEFAULT_TX_DBM = 17.0       # src/config.h:346 LORA_POWER. The compiled-in
                            # default; no testbed node actually runs at it — the
                            # per-node change-config scripts override to 2 or 14.
RFO_THRESHOLD_DBM = 2.0     # RadioLib SX1278.cpp:322: useRfo = (power < 2).
                            # Strict <, so 2 dBm is PA_BOOST by one dB.
VOLTAGE_V = 3.3

# ── [SOLER] measured, PA_BOOST path, PaDac=0x84, 3.3 V ───────────────────────
# NB: their 0/2/4 dBm entries are bit-identical, so 46.71 is best read as the
# *low-PA_BOOST floor* — which is 2 dBm, PA_BOOST's minimum. Say it that way.
PA_BOOST_MEASURED_MA = {
    2.0: 46.71,     # +/- 1.33
    6.0: 65.28,     # +/- 1.70
    8.0: 76.45,     # +/- 1.90
    10.0: 89.33,    # +/- 2.23
    12.0: 102.39,   # +/- 2.81
    14.0: 112.91,   # +/- 2.23
    20.0: 133.28,   # +/- 3.81
}
# [SOLER] measured, RFO path. Here for completeness and for the auto-flip below
# +2 dBm — not used by any current testbed config.
RFO_MEASURED_MA = {2.0: 14.41, 14.0: 25.64}

# ── [DS] Table 6 p.14. Band 1 = 862-1020 MHz = our 869.9 MHz deployment. ─────
RX_BAND1_LNABOOST_OFF_MA = 10.8   # IDDR
RX_BAND1_LNABOOST_ON_MA = 11.5    # IDDR — LoRaMesher leaves LnaBoost on
RX_BAND23_MA = 12.0               # IDDR — 433 MHz, i.e. what [SOLER] measured
STANDBY_MA = 1.8                  # IDDST, crystal on (1.6 typ / 1.8 max)

# [SOLER] dataset, hardcoded as i_sleep = 1.7715e-05 A, commented there as
# "measured by 1000 iteration averaging method". Shield-level: the chip alone is
# 0.2 uA [DS], so this is passives/leakage. The model's old 1.0 mA was ~56x high.
SLEEP_MA = 0.0177

# [DS] Table 6, for cross-check only — NOT the deployed operating points.
# 20 dBm is PaDac=0x87 (high-power mode, separate 1% duty limit and VSWR
# restriction); 17 dBm is PaDac=0x84. They are different PA configurations, so
# fitting a curve through both conflates two bias modes. Do not do it.
PA_BOOST_DATASHEET_MA = {17.0: 87.0, 20.0: 120.0}
RFO_DATASHEET_MA = {7.0: 20.0, 13.0: 29.0}

# The pre-fix table, kept verbatim so `source="legacy"` can reproduce published
# numbers exactly. Its comment claimed "ESP32 + SX1276 module at SF9, 17 dBm",
# but all three claims were wrong: RX 12.0 is radio-only (an ESP32 alone draws
# far more), TX 120.0 is the 20 dBm figure, and no node ran 17 dBm.
_LEGACY_TABLE = {
    "TX": 120.0, "RX": 12.0, "SLEEP": 1.0, "IDLE": 5.0,
    "DISCOVERY_TX": 120.0, "DISCOVERY_RX": 12.0,
    "CONTROL_TX": 120.0, "CONTROL_RX": 12.0,
    "SYNC_BEACON_TX": 120.0, "SYNC_BEACON_RX": 12.0,
}

# Evidence base selector. NOT "measured" — that would be a lie, and it was one:
# under this table TX and SLEEP come from an SMU characterisation but I_RX comes
# from the datasheet, because no band-1 RX measurement exists to cite. The name
# has to say "best available per state", not "measured", or the code misrepresents
# its own provenance. PROVENANCE below records which is which, per state.
SOURCES = ("best", "datasheet", "legacy")

# Accepted spelling of the old name, normalised away. Kept so previously written
# duty_cycle.json / aggregated.json and any saved commands keep working.
_SOURCE_ALIASES = {"measured": "best"}

# Per-state evidence base under source="best". Surfaced by describe() so a run's
# JSON records where each number came from, rather than one blanket label.
PROVENANCE = {
    "TX": "measured (SOLER SMU, PA_BOOST, PaDac=0x84, 433 MHz — band-transplanted)",
    "RX": "datasheet (SX1276 Rev 7 Table 6, band 1, LnaBoost on) — NOT measured",
    "SLEEP": "measured (SOLER dataset, shield-level 17.7 uA)",
    "IDLE": "datasheet (SX1276 Rev 7 Table 6, IDDST standby)",
}


def _norm_source(source: str) -> str:
    """Normalise a source name, accepting the retired 'measured' spelling."""
    s = _SOURCE_ALIASES.get(source, source)
    if s not in SOURCES:
        raise ValueError(f"unknown source {source!r}; expected one of {SOURCES}")
    return s

# Slot-type labels that draw the TX and RX currents respectively. The v2
# firmware logs coordination slots separately, but a sync beacon transmission is
# just a transmission — same PA, same current.
_TX_STATES = ("TX", "DISCOVERY_TX", "CONTROL_TX", "SYNC_BEACON_TX")
_RX_STATES = ("RX", "DISCOVERY_RX", "CONTROL_RX", "SYNC_BEACON_RX")


def pa_path_for(power_dbm: float, force_rfo: bool = False) -> str:
    """Which PA pin RadioLib will drive at this power. Mirrors SX1278.cpp:322.

    Nothing in this project passes force_rfo, so in practice the only way to
    land on RFO is to configure `lora_power` below 2 dBm — where RadioLib flips
    silently and returns no error.
    """
    return "RFO" if (power_dbm < RFO_THRESHOLD_DBM or force_rfo) else "PA_BOOST"


def _interp(grid: dict[float, float], power_dbm: float) -> float:
    """Linear interpolation between measured grid points.

    Deliberately clamps rather than extrapolates. The measured curve is concave
    (the PA driver stays biased, so current does not scale toward zero with
    output power) — extrapolating a straight line off either end produces
    nonsense. Off the low end that matters most: an affine-in-mW fit through the
    datasheet's 17/20 dBm pair predicts ~55 mA at 2 dBm against the measured
    46.7, and an affine-in-dBm fit predicts -78 mA.
    """
    xs = sorted(grid)
    if power_dbm <= xs[0]:
        return grid[xs[0]]
    if power_dbm >= xs[-1]:
        return grid[xs[-1]]
    for lo, hi in zip(xs, xs[1:]):
        if lo <= power_dbm <= hi:
            span = hi - lo
            w = (power_dbm - lo) / span if span else 0.0
            return grid[lo] + w * (grid[hi] - grid[lo])
    raise AssertionError("unreachable: power_dbm within grid bounds")


def tx_current_ma(power_dbm: float, pa_path: str | None = None,
                  source: str = "best") -> float:
    """Radio supply current while transmitting at `power_dbm`, in mA."""
    source = _norm_source(source)
    if source == "legacy":
        return _LEGACY_TABLE["TX"]
    if pa_path is None:
        pa_path = pa_path_for(power_dbm)
    if source == "best":
        grid = PA_BOOST_MEASURED_MA if pa_path == "PA_BOOST" else RFO_MEASURED_MA
    else:  # datasheet
        grid = PA_BOOST_DATASHEET_MA if pa_path == "PA_BOOST" else RFO_DATASHEET_MA
    return _interp(grid, power_dbm)


def rx_current_ma(source: str = "best") -> float:
    """Radio supply current while receiving, in mA. ALWAYS a datasheet value.

    There is no band-1 RX measurement to cite, so source="best" returns the same
    datasheet number as source="datasheet". This is deliberate and is why the
    enum is not called "measured": see PROVENANCE.

    This is the constant that decides the v1-vs-v2 energy comparison: v1 is
    ~99.3% RX and v2 ~72.8%, so I_RX scales both protocols while I_TX barely
    touches v1. The two tie at I_RX = 13.09 mA; every value verifiable for
    band 1 sits below that, but not by more than the spread between [SOLER]'s
    own two RX figures — hence "indistinguishable", never a percentage advantage.

    Band 1 with LnaBoost on is the honest choice for a 869.9 MHz deployment.
    [SOLER]'s 12.27 mA is a bands-2&3 (433 MHz) measurement and reads high here.
    """
    if _norm_source(source) == "legacy":
        return _LEGACY_TABLE["RX"]
    return RX_BAND1_LNABOOST_ON_MA


def current_table(power_dbm: float = DEFAULT_TX_DBM, pa_path: str | None = None,
                  source: str = "best") -> dict[str, float]:
    """Full per-slot-type current table (mA) for a node at `power_dbm`.

    Keys match the v2 firmware's `Slot N transition: type=` labels, plus IDLE.
    Unknown labels are the caller's problem — both consumers use .get(k, 0.0),
    which counts an unrecognised slot type as drawing nothing.

    Under source="best" the table is MIXED provenance: TX and SLEEP measured, RX
    and IDLE from the datasheet. See PROVENANCE.
    """
    source = _norm_source(source)
    if source == "legacy":
        return dict(_LEGACY_TABLE)
    i_tx = tx_current_ma(power_dbm, pa_path, source)
    i_rx = rx_current_ma(source)
    table = {"SLEEP": SLEEP_MA, "IDLE": STANDBY_MA}
    table.update({k: i_tx for k in _TX_STATES})
    table.update({k: i_rx for k in _RX_STATES})
    return table


def describe(power_dbm: float, pa_path: str | None = None,
             source: str = "best") -> dict:
    """Provenance record for one node, for embedding in duty_cycle.json.

    Carries per-state provenance rather than one blanket label, so a run's JSON
    cannot be read as "these currents were measured" when only two of them were.
    """
    source = _norm_source(source)
    path = pa_path or pa_path_for(power_dbm)
    return {
        "tx_power_dbm": power_dbm,
        "pa_path": path,
        "source": source,
        "tx_current_mA": tx_current_ma(power_dbm, path, source),
        "rx_current_mA": rx_current_ma(source),
        "provenance": dict(PROVENANCE) if source == "best" else source,
    }


def _selftest() -> None:
    assert pa_path_for(2.0) == "PA_BOOST", "2 dBm must stay on PA_BOOST"
    assert pa_path_for(1.9) == "RFO", "below 2 dBm RadioLib flips to RFO"
    assert pa_path_for(14.0) == "PA_BOOST"
    assert pa_path_for(20.0, force_rfo=True) == "RFO"

    for dbm, want in PA_BOOST_MEASURED_MA.items():
        got = tx_current_ma(dbm, source="best")
        assert abs(got - want) < 1e-9, f"grid point {dbm} dBm: {got} != {want}"

    # The retired "measured" spelling still resolves, so old JSON and saved
    # commands keep working.
    assert tx_current_ma(2.0, source="measured") == tx_current_ma(2.0, source="best")
    assert current_table(2.0, source="measured") == current_table(2.0, source="best")

    # RX is a datasheet value under every non-legacy source. This is the whole
    # reason the enum is "best" and not "measured".
    assert rx_current_ma("best") == rx_current_ma("datasheet") == RX_BAND1_LNABOOST_ON_MA
    assert "NOT measured" in PROVENANCE["RX"]

    assert tx_current_ma(0.0) == RFO_MEASURED_MA[2.0], "clamps, never extrapolates"
    assert tx_current_ma(25.0) == PA_BOOST_MEASURED_MA[20.0], "clamps at the top"
    assert current_table(source="legacy") == _LEGACY_TABLE

    mid = tx_current_ma(7.0)
    assert PA_BOOST_MEASURED_MA[6.0] < mid < PA_BOOST_MEASURED_MA[8.0]

    print("PA path (RadioLib SX1278.cpp:322 — useRfo = power < 2):")
    for dbm in (1.0, 1.9, 2.0, 14.0, 17.0, 20.0):
        print(f"  {dbm:>5.1f} dBm -> {pa_path_for(dbm)}")

    print("\nTX current by source (mA):")
    print(f"  {'dBm':>5}  {'measured':>9}  {'datasheet':>10}  {'legacy':>7}")
    for dbm in (2.0, 14.0, 17.0, 20.0):
        print(f"  {dbm:>5.1f}  {tx_current_ma(dbm):>9.2f}  "
              f"{tx_current_ma(dbm, source='datasheet'):>10.2f}  "
              f"{tx_current_ma(dbm, source='legacy'):>7.2f}")

    print("\nDeployed table @ 2 dBm (12 of 13 active nodes):")
    for k, v in sorted(current_table(2.0).items()):
        print(f"  {k:<16} {v:>8.4f} mA")

    print("\nWhat changed vs legacy, at the deployed 2 dBm:")
    new, old = current_table(2.0), current_table(source="legacy")
    for k in ("TX", "RX", "SLEEP", "IDLE"):
        print(f"  {k:<6} {old[k]:>7.2f} -> {new[k]:>7.4f} mA")


if __name__ == "__main__":
    _selftest()
