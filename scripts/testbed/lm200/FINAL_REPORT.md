# LoRaMesher 2.0.0 (764b893): testbed evaluation, final report

**Date:** 2026-10-10.
**Branch:** LoRaChat `lm200-eval`.
**Library:** LoRaMesher `release/2.0.0` @ 764b893, RadioLib 7.7.1 (pinned, the July driver).
**Baseline:** 134ae25, the July/paper v2 data, plus an interleaved A/B run in this campaign.
**Testbed:** 16 TTGO T-Beams on 7 gateways. A 13-node main cluster, plus a 3-node remote cluster (DD58/5D58, 14A4, 3428) joined over a 3.5 km link (006C↔3428).
**Campaign:** 2026-10-07 14:48 → 2026-10-10 07:56 UTC, unattended (`lm200/campaign.py`).
**Generated outputs:**
- `runs/lm200_campaign/report/report.md` and `verdict.json`, the automated verdict;
- `runs/lm200_campaign/gates/g{0,1,2}.json`, the per-check data;
- `stack_warnings/`, the stack headroom for every run.

---

## 1. Verdict

The automated verdict is **NO-GO** (21 blockers, 9 caveats). Most of those "blockers" are the same strict stack threshold hitting every run. Reviewed one by one, they come down to:

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Stop() → Start() crashes or deadlocks**, 0/9 cycles recovered | **Release blocker** | Filed: LoRaMesher #112, high priority |
| 2 | **All-AUTO network fragments into several managers that never merge** | **Release blocker (proposed)** | New, not filed yet (§7) |
| 3 | LoRaMeshMain stack 448 B free of 4096, RadioEvent 484 B free of 3280 | Fix before release (easy) | Raise the stack sizes (§9) |
| 4 | SF9 reliable delivery with 2 data slots: ACK 0.47 | Unexplained, single run | Repeat (§6) |
| 5 | SF7 cold-start formation slower than July (532 s vs 385 s median) | Caveat (GREY), config differs from July | Re-test with a clean config (§4, §10) |
| 6 | `max_data_slots` overflow: the manager rejects joins past capacity | Behaviour change, by design | Document in the CHANGELOG (§8) |

**Everything else passed:**
- the smoke test;
- the regression against July;
- the interleaved A/B (2.0.0 equal or better);
- reliable unicast with 3 data slots (100% ACKed at SF7 and SF9);
- group messaging;
- restart of a pinned manager;
- the SF11 cold-start join;
- the long link at SF10.

No crashes, hangs, or memory leaks outside the Stop/Start test.

**Recommendation:** do not tag 2.0.0 until #1 and #2 are fixed and the stack sizes in #3 are raised. Then re-run only the affected cells: stopstart_SF9, nm_failover_auto_SF9, and reliable_SF9_dds2 ×3.

---

## 2. What was run

| Stage | Content | Runs |
|---|---|---|
| G-1 local | Library native tests; all v2 envs build | — |
| T0 smoke (G0) | SF9, 16 nodes, 20 min | 1 |
| T1a regression (G1) | formation SF7/9/12 ×3; 13node SF7/9/12; reach16 SF9/12; dataslots 2/4 | 18 |
| T1b A/B | 13node SF7/9/12, 134ae25 vs 2.0.0, interleaved, 3 rounds | 17 + 1 recovered |
| T2 features (G2) | reliable, NM restart, slot overflow, group, Stop/Start, SF11 join ×2, long link SF10 (2 h) | 9 |
| T2 re-run | reliable with a 10-min RTT tail plus a data-slot sweep (1/2/3 slots at SF7 and SF9); NM election with all nodes AUTO | 7 |

---

## 3. G0 smoke: PASS (14/14)

- **Build:** flash 16/16; library `2.0.0+sha.764b893`; RadioLib 7.7.1.
- **Stability:** 0 crashes, 0 reboots, 0 LDRO/time-on-air warnings.
- **Network:** 13/13 joined, 13/13 complete routing tables, exactly one manager, fair PDR **0.97**.
- **Superframe:** 37 slots, the same as the July modal value. Join requests used even discovery slots, as designed.

## 4. G1 regression vs July 134ae25: GREY → A/B triggered

| Metric | 2.0.0 | July 134ae25 | Result |
|---|---|---|---|
| Formation SF7, convergence median | 532 s (n=2) | 385 s [256–611] | **GREY** |
| Formation SF7, join retries per run | 22.3 | 8.8 | **GREY** |
| Formation SF9, convergence median | **376 s** | 624 s | PASS, faster |
| Formation SF12, convergence median | **1845 s** | 2479 s | PASS, faster |
| Join retries per run SF9 / SF12 | 6.0 / 18.7 | 8.2 / 22.2 | PASS (fewer retries at high SF) |
| 13node fair PDR SF7 / SF9 / SF12 | 0.993 / 0.992 / **1.000** | 0.997 / 0.996 / 0.988 | PASS |
| reach16 fair PDR SF9 / SF12 | **0.888 / 0.908** | 0.805 / 0.800 | PASS, +0.08 / +0.11 |
| Data slots granted (2 and 4 slots) | as requested | — | PASS |
| Two managers at once | never | — | PASS |

**SF7 formation:**
- **Main cluster:** always converges, in 495–688 s.
- **Remote cluster:** missing in 1 of 3 runs (14A4 and DD58). This is the known asymmetric long link at SF7, the same mechanism as July.
- **Slower and more retries:** most likely the new random join backoff (0–3 superframes), which costs most at SF7, where superframes are short and many nodes start together.
- **Correction (config leak):** all nine `lm200_formation` runs ran with `SIM_PDR_COMPARE 1` inherited from the smoke run. `configtool/generate.py` only rewrites the keys a batch lists, and the formation retry skipped the `git reset` in `deploy.sh upgrade`. The logs show about 2,000 `Simulator (testbed PDR mode)` lines per SF7/SF9 run and about 10,100 per SF12 run. The July `formation__lmv2` runs show none.
  - **No app traffic was sent:** 0 `APP_TX`, so airtime matched July.
  - **But the firmware differed from July:** WiFi and MQTT were off, and the WiFi-equipped sink called `setGateway()`.
  - **So the SF7 GREY is not a clean comparison.** It is re-tested in the next campaign with a clean config. The leak is fixed there (`config.h` is reset before every upload, and a per-node `CFG` boot line is checked against `config.yaml`).

## 5. Interleaved A/B, 13 nodes (134ae25 vs 2.0.0, same days, alternating)

| Cell | Version | Runs | Fair PDR (range) | Latency p50 | Latency p95 | Mean current |
|---|---|---|---|---|---|---|
| SF7 | 134ae25 | 3 | 0.998 (0.993–1.0) | 22.7 s | 44.0 s | 13.45 mA |
| SF7 | **2.0.0** | 3 | 0.998 (0.993–1.0) | 22.1 s | 43.9 s | 13.46 mA |
| SF9 | 134ae25 | 3 | 0.982 (0.962–1.0) | 35.6 s | 95.4 s | 12.95 mA |
| SF9 | **2.0.0** | 3 | 0.969 (0.923–1.0) | 33.8 s | **66.0 s** | 12.90 mA |
| SF12 | 134ae25 | 2 | 0.74 (0.72–0.76) | 129.7 s | 228.8 s | 13.42 mA |
| SF12 | **2.0.0** | 3 | **0.933** (0.90–0.96) | 129.4 s | 223.8 s | 13.85 mA |
| SF12 | **2.0.0, valid runs only** | 2 | **0.92** (0.90, 0.94) | 124.4 s | — | — |

- **SF7 and SF9:** no difference beyond run-to-run noise. 2.0.0 has a tighter p95 at SF9.
- **SF12:** 2.0.0 delivers about 0.18 more, and convergence is faster: 967 s vs 1408 s, from the valid runs only.
- **Correction (host suspend):** the 2.0.0 SF12 round-2 run (`lm200ab_13node-20261008-210609-SF12_d308000-r02`) covers the 8.3 h PC suspend. Its measurement window is 538 min instead of 40, so late deliveries count (PDR 0.96), and it is the source of the 96 mJ/bit energy outlier.
  - It was wrongly accepted as valid. It is now excluded: the row "valid runs only" above, and `runs/EXCLUDED.txt`.
  - Without it, the conclusion holds: 2.0.0 delivers 0.92 vs 0.74 at SF12.
  - The validator now rejects host suspends and over-long windows.
- **SF12 134ae25 round 3:**
  - A Windows file lock blocked the move, and the script then deleted it.
  - The logs were recovered from the gateways (`runs/lm200ab_13node__lm134.recovered/`). Without lifecycle.json it is indicative only and not in the table.
  - The script is now fixed so it never deletes.
- **A/B figures:** the automated report links `figures_ab/`, but those figures were not generated. The numbers above come from `runs/lm200ab_13node__lm{134,200}/aggregated.json`.

## 6. Reliable unicast and the data-slot sweep

13 nodes, every node sends to 006C with `SendReliable`. Each run logged for 10 more minutes after the last send, so round trips could finish. The SF7 cells are 15 messages per node at 60 s intervals; the SF9 cells are 10 per node at 95 s.

| SF | Data slots | ACKed / sent | ACK ratio | Failed after retries | Rejected (table full) | Unresolved at end | RTT p50 | RTT p95 |
|---|---|---|---|---|---|---|---|---|
| 7 | 1 | 145/150 | 0.97 | 5 | 4 | 0 | 197 s | 260 s |
| 7 | 2 | 147/150 | 0.98 | 3 | 3 | 0 | 133 s | 179 s |
| 7 | **3** | **150/150** | **1.00** | 0 | 0 | 0 | **45 s** | **86 s** |
| 9 | 1 | 72/99 | 0.73 | 3 | 1 | 24 | 314 s | 379 s |
| 9 | 2 | 47/100 | 0.47 | 18 | 14 | 35 | 104 s | 293 s |
| 9 | **3** | **100/100** | **1.00** | 0 | 0 | 0 | **107 s** | **222 s** |

- **Three data slots per node fix reliable delivery at both SFs.** Every message was ACKed, nothing was rejected or left in flight, and RTT fell 3–4×.
  - With one slot, the round trip (one superframe per hop each way, plus retries) is longer than the send interval. The 8-entry delivery table fills, and late outcomes spill past the window.
  - A default of 3 slots, or advice to use ≥ 3 for reliable traffic, is the main tuning result of this campaign.
- **SF9 with 2 slots is an outlier and needs a repeat.**
  - It is the only cell where `Queue full for slot type TX` appears: 45 times, all on 34DC.
  - 2AD4, 4270 and 7B6C delivered 3/10. All three are direct neighbours of the sink, with good link quality, so this is not routing or a relay bottleneck.
  - The testbed was healthy: 16/16 logs, one manager, no reboots.
  - SF7 with 2 slots behaved normally. Repeat ×3 before concluding anything, and check the data-slot layout at 2 slots with SF9 timing.
- **SF9 with 1 slot:** 24 sends were still unresolved 10 minutes after the last send. At that load the worst-case round trip exceeds 10 minutes.
- **No RTT outliers:** none from the unclamped `echo_ts`, 0 duplicate ACKs.

## 7. Manager failover and election

**Pinned manager** (`LORA_MANAGER_ID=0x006C`): PASS. The manager was restarted 3 times and came back as manager within **2.6 s** each time. There was never a second manager, and there were no reboots other than the intended ones.

**All nodes AUTO** (`LORA_MANAGER_ID=0`, SF9, 13 nodes, 62 min; the node acting as manager restarts at 20 min): **FAIL. The network fragmented and never merged.**

Self-elections, from the device logs (times are log-local, UTC+2):

| Time | New manager | Context |
|---|---|---|
| 08:48:50 | 006C | cold start |
| 08:49:01 | 2A9C | cold start, 12 s later: **two networks from the start** |
| 08:54:51 | 7B6C | 3 nodes' election backoff expired together (7B6C, 34DC, 4270) |
| 09:01:18 | 4B34 (CB34) | another election |
| 09:08:07 | — | failover test restarts 4B34, the manager at that moment |
| 09:11:01 | 7984 | takeover, 173 s after the restart |
| 09:14:10 | 5F10 (DF10) | |
| 09:19:19 | 5D3C (DD3C) | |
| 09:22:14 | 7B6C | |

- **End of the run (09:50):** three managers coexist, and have done so for about 30 minutes:
  - 5F10 with 006C, 1484, 2A9C, 34DC, 4270, 4B34, 6464, 7984;
  - 5D3C with 77A4;
  - 7B6C with 2AD4.
- **Timeline:** two or more managers overlapped for 2,617 s of the run.
- **Node behaviour:** nodes kept switching networks, for example 7B6C: 2A9C → 006C → self.
- **No crashes or hangs.**

**What it means:**
- **Takeover works:** a new manager appears within 3 minutes.
- **Nodes start as two managers:** with every node AUTO, a cold start produces two managers within seconds.
- **Separate managers never merge:** once two managers exist in overlapping radio range, nothing reconciles them.
- **Single run, but the failure is not marginal:** the CHANGELOG lists dual-manager election fixes for 2.0.0.
- **Pinned deployments are not affected:** the tested deployments that pin a manager (G0, G1, A/B, all other T2) never had two managers.

**Proposed:** file this as a second high-priority 2.0.0 issue next to #112. Attach `runs/lm200_features_rerun/*-nm_failover_auto_SF9-r01/logs`.

## 8. Other feature tests

**Group (SF9, all 13 joined, one sender, with ACKs): PASS.**
- 98.75% of member deliveries received, 0 duplicates.
- Group ACK completeness was 6.25%, against about 28% noted upstream at 25 nodes. That is informational, a known open item.

**Stop/Start (SF9, 3 nodes Stop for 120 s and then Start, every 10 min): FAIL, 0 of 9 cycles recovered.** This is LoRaMesher #112.
- **2A9C and B4DC:** `assert failed: vTaskResume tasks.c:2136` on every Start. `Start()` resumes the protocol task that `Stop()` deleted. The nodes rebooted 5 and 4 times.
- **CB34:** the application task that called `Stop()` never ran again. HEALTH stopped, and the radio queue overflowed (`Failed to send radio event to queue`). This is consistent with the task being deleted while it held a lock.
- #112 lists the three root causes and a regression test.

**Slot overflow (`max_data_slots 8`, 1 slot per node, 13 nodes): behaviour change, not a fault.**
- The manager admits nodes until the slots run out. After that it logs `No slots available, rejecting node 0x…` and replies to the join with status 1, slots 0.
- The rejected nodes retry every few superframes indefinitely: 20–24 rejections each. 4 nodes never joined during the run, so no routing table was complete.
- No crashes; one manager.
- 134ae25 instead admitted everyone with partial slot allocation. The 2.0.0 CHANGELOG should state the new admission control, and that `max_data_slots` caps the network size.
- The G2 checks `slot_overflow:join` and `routing_tables` FAIL because they assumed the old behaviour. Treat them as expected.

**High-SF cold start (SF11, 16 nodes at once, ×2): PASS.**
- 16/16 joined, 16/16 complete routing tables, one manager.
- Join backoff spread over 0–3 superframes (`{0:5, 1:8, 3:2}` and `{0:4, 1:8, 2:1, 3:2}`), with no lockstep collisions.
- Minimum free heap was about 72 KB, against about 157 KB in the other runs. That is fine, but worth knowing at SF11.

**Long link (SF10, 20 dBm, 16 nodes, 2 h): PASS.**
- All 3 remote nodes joined and routed through 006C↔3428, and the bridge 3428 never lost its join (0 flaps).
- Remote-flow PDR over the link was 3428 0.575 and 14A4 0.525: 40 messages each, p50 latency 70–99 s.
- The DD58 (5D58) flow was not captured by the metric. That is an address-remap gap in `lm200_metrics`, still to check.

## 9. Stack headroom (all 2.0.0 runs, 655 node×run samples per task)

| Task | Configured | Worst free | Typical free (median) | Proposed |
|---|---|---|---|---|
| LoRaMeshMain | 4096 B | **448 B** | 580 B | **5120 B** |
| RadioEvent | 3280 B | **484 B** | 788 B | **4096 B** |
| SuperframeUpdate | 3072 B | 1072 B | 1344 B | keep |

- No stack overflow happened in any run, but LoRaMeshMain and RadioEvent log `stack low` constantly: about 63k and 67k warnings.
- The campaign gate (≥ 512 B free) flags almost every run, which is why there are so many "blockers" in `verdict.json`.
- Per-run detail is in `stack_warnings/<run>/` and the cross-run summary in `stack_warnings/SUMMARY.md`.
- The 1 B and 13 B SuperframeUpdate readings in the first G2 output were serial lines cut off by another task. The parsers now reject them (`gates.py` `_RE_STACK`, `grab_stack_warnings.py`), and G2 and the report were regenerated.

## 10. Data quality and testbed incidents

| Incident | Effect | Handling |
|---|---|---|
| RadioLib pinned to 7.5.0 by mistake (stale local cache) | First launch used the wrong driver | Re-pinned to 7.7.1, the July version; relaunched from scratch. All reported data is 7.7.1, checked in every gate |
| VPN drops; GW-7 needs a key-only ssh-agent | Early launch attempts failed | Agent socket set up; relaunched |
| G1 membership logic counted only self-reported joins | False FAIL on SF7 join completeness | Fixed (nodes seen in other nodes' tables count as joined); you confirmed 13/13 converge |
| Validator rejected valid warm-up batch runs | Formation r01 runs marked invalid | Fixed and the runs restored |
| PC suspend froze WSL for about 8 h | Campaign paused; the A/B run in progress (2.0.0 SF12 r02) got a 538-min window and was wrongly accepted | Run excluded (§5); keep-awake used for the rest of the campaign; validator now checks for host suspend and the window length |
| Config leak between batches | All `lm200_formation` runs had `SIM_PDR_COMPARE 1` from the smoke run (WiFi/MQTT off, sink as gateway, no traffic) | SF7 GREY marked as not a clean comparison (§4); `config.h` is reset before each upload and checked with the `CFG` line from the next campaign |
| Windows file lock + `rm -rf` fallback in run_full_redo.sh | One A/B run (SF12 134ae25 r3) deleted locally | Logs recovered from the gateways and kept out of the aggregate; the script never deletes now |
| First reliable runs logged only 2 min after the last send; first failover test used a pinned manager | Reliable outcomes truncated; election untestable | Re-run batch with a 10-min tail and AUTO roles (§6, §7); the old cells are superseded in G2 |
| Interleaved serial lines | Bogus stack readings of 1 B and 13 B | Parser fixed (§9) |
| Unpinned RadioLib 7.8.1 in each gateway's libdeps | None: not compiled | Shown in the report as "not compiled" |

## 11. Next steps

1. **LoRaMesher:**
   - fix #112;
   - fix the AUTO multi-manager merge (file the issue);
   - raise the LoRaMeshMain stack to 5 KB and RadioEvent to 4 KB;
   - document slot-admission control;
   - consider `default_data_slots = 3`, or document it for reliable traffic.
2. **Re-test with the fixed build:**
   - `stopstart_SF9`;
   - `nm_failover_auto_SF9` ×3;
   - `reliable_SF9_dds2` ×3;
   - one SF7 formation ×3, to settle the GREY.

   Use `campaign.py --only-stage T2` with a trimmed batch, about 6 h.
3. **LoRaChat housekeeping, left to you:**
   - merge `lm200-eval` into `new_loramesher` once 2.0.0 is tagged;
   - restore `GIT_BRANCH` in `testbed.conf`;
   - decide whether to commit `stack_warnings/` (12 MB, mostly the raw `warnings.log` files);
   - generate the missing A/B figures (`plot_compare.py --v1-label 134ae25 --v2-label 2.0.0`).
