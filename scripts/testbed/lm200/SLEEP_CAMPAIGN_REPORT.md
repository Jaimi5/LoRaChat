# LoRaMesher 2.0.0 fix + member-sleep: testbed campaign, final report

**Dates:** 2026-10-11 16:21 UTC → 2026-10-13 07:30 UTC (unattended, `lm200/campaign.py --config lm200/campaign_sleep.yaml`).
**Branch:** LoRaChat `lm200-sleep`.
**Firmware arms** (same app, same send loop outside the sleep cells):
- **FIX** = `ttgo-t-beam-v2`, LoRaMesher `release/2.0.0` @ `ab9449a`: 764b893 plus the Stop/Start fix (#112, PR #135) and the AUTO warning.
- **SLP** = `ttgo-t-beam-v2-sleep`, LoRaMesher `feat/member-sleep` @ `86a4072`: the same fix merged into member-sleep (light and deep sleep for members).

**Radio driver:** RadioLib 7.7.1, pinned and linked in both arms (upload dependency graph on every board). The report's "unpinned copy" table shows 7.8.1 for the SLP env: that is LoRaMesher's own transitive copy in `.pio/libdeps`, not compiled.
**Testbed:** 16 TTGO T-Beams on 7 gateways. A 13-node main cluster, plus a 3-node remote cluster (DD58, 14A4, 3428) over a 3.5 km link (006C ↔ 3428). Manager 006C pinned (NETWORK_MANAGER + NODE_ONLY).

**Outputs:**
- `runs/lm200_sleep_campaign/report/report.md`: automated verdict, every gate and check;
- `runs/lm200_sleep_campaign/report/stats.md` and `stats.json`: the statistical comparison (§10);
- `runs/lm200_sleep_campaign/gates/*.json`: per-check data;
- `analysis/stats_compare.py`: reproduces §10.

---

## 1. Verdict

The automated verdict says **NO-GO, 3 blockers**. Two are the group cell (`g2-N2`, and `g2-N2b`, which pools N2's failed run with a passing repeat; see #8). The third is `ab-S11`, the SF12 convergence comparison with **one run per arm**. Repeated to three (`ab-S11b`) and then four runs per arm (`ab-S11c`), it is **GREY**, and the difference is not statistically significant (p = 0.21), so that blocker is not real. The group failure is real but intermittent (#8). Reviewed one by one:

| # | Finding | Severity | Evidence |
|---|---|---|---|
| 1 | **Stop()/Start() works on hardware (#112 fixed)** | Release blocker closed | S3 FIX: 21/21 cycles rejoined, worst 2.69 superframes, 0 crashes/hangs. S4 SLP: 28/28, statistically equivalent to FIX within ±1 superframe |
| 2 | **Member deep sleep works end to end** | Feature verified | S7: 12/12 members, ~450 deep sleeps per run, resume 99–100 %, 0 rejoins, PDR equal to control |
| 3 | **Deep sleep saves less energy than light sleep at this superframe** (modelled) | Tuning | Chip −36.6 % (deep) vs −40.2 % (light); boot costs ~3.5 s per wake (§6) |
| 4 | SF12 convergence: SLP +1.2 superframes on average, **not significant** | Watch | n = 4/4: +149 s, p = 0.21, run-to-run spread as large as the difference; n = 3 had p = 0.037, which the 4th run removed. SF9 identical; PDR and latency unaffected (§4) |
| 5 | **Reliable unicast: the sink drops ACKs when its TX queue is full** | Library improvement | Data 100 % delivered; ACK loss follows the busiest sink's load (Spearman ρ = 0.94, p = 0.005). Up to 48 % of sends still in flight at window end, even with 3 data slots when one sink takes 95 % of the traffic (§8) |
| 6 | Formation at SF7: main cluster always joins; remote cluster does not | Known link limit | 36/36 member joins; 3428/14A4/DD58 fail at SF7 as in July (§7) |
| 7 | Testbed: ~50 s of serial log lost after every reset | Measurement artefact | Join times < 60 s after reset unreliable; analysis fixed, pipeline fix proposed (§11) |
| 8 | **Group messaging, 0 % received in 1 of 2 runs (FIX): the network stopped listening to the sender's data slot** | Library bug candidate (intermittent), release arm | The manager marked its strong direct link to the sender as one-way, routed it via a relay and dropped its RX entry (§9, corrected 2026-10-13). Passed in lm200-eval (13 nodes, 98.75 %) |

**Recommendation:**
- **Release 2.0.0 with the Stop/Start fix (FIX), after triaging the group-slot loss (#8).** It occurred on FIX in 1 of 2 group runs. At minimum, file it as a known issue with the N2 logs; whether it blocks the release is the maintainers' call.
- **Member-sleep is functionally ready** (light and deep) and matches FIX wherever it was measured. Two follow-ups come before it is the default: the deep-sleep energy break-even (§6) and more SF12 repeats (§4).
- **Reliable ACK handling (§8)** is a library issue in both arms.

Details in §12.

---

## 2. What was run

| Stage | Arm | Cell | Runs | Gate |
|---|---|---|---|---|
| S1 / S2 | FIX / SLP | 13 nodes SF9, one packet per node every 63 s | 2 + 2 | G0 GREY ×2 (no July SF9 reference), **ab PASS** |
| S3 / S4 | FIX / SLP | Stop/Start every 5 min, 60 s off, 3 / 4 nodes (S4: 2 plain + 2 SaveState) | 1 + 1 | **g2 PASS** / g2 GREY |
| S5–S7 | SLP | sleep control / light / deep, SF9, min sleep fraction 0.5 | 1 + 1 + 2 | **sleep PASS** (37/37) |
| S8 / S9 | FIX / SLP | formation SF7, 16 nodes | 3 + 3 | INCOMPLETE ×3 (remote cluster) |
| S10/S10b / S11/S11b | SLP / FIX | 13 nodes SF12 | 3 + 3 | G1 GREY (no July SF12), ab GREY (ab-S11 with n = 1: FAIL) |
| S12 | FIX | reliable SF9, 2 data slots ×3, 3 data slots ×1 | 4 | g2 GREY (in flight at window end) |
| N1 | FIX | reliable SF9, 3 data slots ×2 more | 2 | g2 GREY (one run 48 % in flight) |
| S10c / S11c | SLP / FIX | 13 nodes SF12, 4th run per arm | 1 + 1 | G1 GREY, ab GREY |
| N2 / N2b | FIX | group messaging, 16 nodes | 1 + 1 | N2: 0 % received (FAIL); N2b: 93.3 % (PASS by itself; the pooled gate is FAIL) |
| N3 | SLP | NM failover, pinned manager | 1 | **g2 PASS** |

The first deep-sleep attempt (2 runs) is excluded and kept in `runs/lm200s_sleep__slp.superseded-minsleep30000/` (§6).

---

## 3. FIX vs SLP at SF9 (sleep off, same send loop)

| Metric | FIX (n = 2) | SLP (n = 2) | Test |
|---|---|---|---|
| Fair PDR | 0.981 | 0.985 | p = 0.80; per sender paired Δ +0.004, 95 % CI [−0.017, +0.024] |
| Convergence from reset | 359 s | 360 s | p = 0.99 |
| App latency p95 | 33.2 s | 34.0 s | ×1.03, p = 0.27 |
| Join retries per run | 7.5 | 4.5 | p = 0.27 |

No difference at SF9. Two runs per arm are too few to prove equivalence per run (TOST), but the per-sender paired PDR test is at the edge of equivalence within ±0.02 (TOST p = 0.053).

---

## 4. FIX vs SLP at SF12

| Metric | FIX (4 runs) | SLP (4 runs) | Welch test |
|---|---|---|---|
| Fair PDR | 1.0 ×4 | 1.0 ×4 | identical |
| App latency p95 (median) | 120.6 s | 121.3 s | ×1.01 |
| Convergence (all routing tables complete) | 1354, 1202, 1238, 1344 s | 1639, 1463, 1439, 1192 s | +149 s (1.2 superframes), **p = 0.21**, 95 % CI [−127, 425] s; Mann-Whitney p = 0.34 |
| Last member join | 1107, 725, 756, 867 s | 1155, 986, 954, 717 s | +89 s, p = 0.50 |
| Join retries | 21, 15, 15, 17 | 23, 16, 25, 17 | +3, p = 0.27 |

- **One run per arm gave the campaign's only FAIL** (ab-S11: 1639 vs 1354 s). At three runs per arm the difference was "significant" (p = 0.037). The fourth SLP run was the fastest of all (1192 s), and at n = 4 nothing is significant.
- **Equivalence within ±1 superframe isn't shown either** (TOST p = 0.60). The honest reading: no detectable difference, and a possible slowdown of up to about a superframe that these runs can't rule in or out.
- **How convergence behaves:** it is set by one straggler node learning its last route, in whole-superframe steps (routing adverts once per 122 s superframe). That makes it noisy.
- **Measurement checks:** SLP prints its routing table more often (every 26–30 s vs 36–52 s), so resolution doesn't penalise it. There is no route aging (0–3 removals per run), and routing traffic is the same.
- **Impact:** none on delivery or latency.

---

## 5. Stop / Start (#112)

| | S3 FIX | S4 SLP |
|---|---|---|
| Cycles / rejoined | 21 / 21 | 28 / 28 |
| Rejoin median / mean | 46.9 s / 53.7 s | 54.8 s / 61.5 s |
| Worst | 94.7 s (2.69 superframes) | 178.5 s (5.08 superframes) |
| Crashes, reboots, hung nodes | 0 | 0 |
| Sequence counter survives restart | yes | yes |
| Stack min free (LoRaMeshMain / RadioEvent / SuperframeUpdate) | 2536 / 1340 / 1200 B | – |

- **Statistics:** SLP − FIX = +7.8 s per cycle, p = 0.28, **equivalent within ±1 superframe** (TOST p < 0.05).
- **Why S4 was GREY:** two cycles of node 2A9C took over 3 superframes. The manager answered `Join request deferred (NM busy)` while 2–3 other nodes were rejoining in the same superframe. S4 restarts 4 nodes on the same period against 3 in S3; NM-busy deferrals were 2 in S4 and 0 in S3.
- **Warm restart** (SaveState → Stop → Start) vs plain on SLP: −10.5 s, not significant, 0 cold fallbacks.
- **#112 can be closed.** The drafted comment is in the scratchpad (`i112.md`).

---

## 6. Member sleep (SLP): control vs light vs deep

Same schedule in all three cells: SF9, min sleep fraction 0.5 (superframe about 66 s with about 33 s SLEEP runs), 9 packets per node, one every 4 min, scheduled send loop.

| Metric | Control | Light | Deep (2 runs) |
|---|---|---|---|
| Members that slept | – | 12/12 (1514 light sleeps) | 12/12 (≈450 deep sleeps per run) |
| Resume success | – | – | 1.0 / 0.987, 0 cold starts, 0 rejoins |
| Fair PDR | 0.989 | 1.0 | 0.989 / 0.989 |
| Latency from the scheduled send (mean) | 33.4 s | 33.8 s (p = 0.87) | 37.6 s (+4.2 s, p = 0.074) |
| Boot rst → BOOT_READY p95 | – | – | 3507 / 3515 ms (budget 4200) |
| rst → schedule confirmed by a beacon, p95 | – | – | 10.8 s |
| Sleep clock error p95 | – | – | 6757 / 6418 ppm (limit 10000) |
| **Modelled** chip / board current | 57.0 / 95.1 mA | 34.1 / 72.2 mA (−40 % / −24 %) | 36.1 / 74.2 mA (−37 % / −22 %) |

- **PDR:** per-sender delivery under deep sleep is statistically equivalent to the control within ±0.05.
- **Latency:** measured APP_TX → APP_RX, sleep looks 6–7 s *faster* (p < 0.02). That is a logging artefact: with the scheduled loop, APP_TX is printed only after the wait for `isReadyToSend()`, and the wait goes to `APP_TX_LATE`. The figures above add `late_ms` back.
- **Gate fixes during the campaign:**
  - **First attempt (excluded): 1/12 members deep-slept.** After the AP8 smoke the boot budget went from 1200 to 4200 ms (measured p95 3471 ms). The library deep-sleeps only if `window − (boot + guard + clock allowance) ≥ min_sleep_ms`, and the 33.1 s runs left about 28.6 s, below the 30 s default. Fixed per cell with `lora_deep_sleep_min_ms: 20000` (20d337a).
  - **One "unexpected reboot" on C270 (r02).** Its `Deep sleep for` line was cut off on the serial port, but the firmware logged `Resumed network … after 29751 ms of deep sleep` 3 s later. The gate now accepts that resume line as proof of a planned wake (9afc965).
- **Deep sleep's energy cost.** Each wake costs about 3.5 s of boot plus the early-wake margin at full MCU current, so at about 33 s windows deep sleep is *worse* than light sleep. About 1.7 s of the boot is the VERBOSE ESP-IDF startup log (`CONFIG_LOG_DEFAULT_LEVEL=5`, ≈20 KB at 115200 baud). Break-even with the model's currents: about 280 s windows today, about 90 s with a 1.2 s boot.
- **Board floor:** the T-Beam's GPS (37 mA) dominates the total. These energy figures are **modelled**, not measured.

---

## 7. Formation at SF7 (16 nodes)

| | S8 FIX (3 runs) | S9 SLP (3 runs) | Test |
|---|---|---|---|
| Main-cluster members joined | 12/12 every run | 12/12 every run | – |
| Last member join from reset | 165, 171, 194 s | 172, 164, 223 s | +9.6 s, p = 0.67 |
| Join retries per run | 15, 18, 20 | 20, 25, 25 | +5.7, p = 0.064 (g = 1.7) |
| Remote cluster | never joined | 3428 joined once | SF7 long-link limit |

- **INCOMPLETE gates:** full 16-node convergence is never reached at SF7, as in July. The 3.5 km link's reverse path is about 6 dB weaker and sits at SF7's threshold.
- **Main cluster:** forms equally fast on both arms. SLP leans towards more join retries; that's not significant at n = 3, but the effect size is large.

---

## 8. Reliable unicast and data slots (FIX)

| Data slots | Run | ACKed / resolved | Busiest sink (messages) | ACK drops at the sink | In flight at window end |
|---|---|---|---|---|---|
| 2 | S12 r01 | 66/66 | 77A4 (99) | 120 | 33 % |
| 2 | S12 r02 | 78/79 | E464 (55) + DF10 (41) | 86 | 21 % |
| 2 | S12 r03 | 78/78 | 77A4 (82) | 95 | 20 % |
| 3 | S12 r01 | 98/98 | DF10 (73) | 50 | 2 % |
| 3 | N1 r01 | 99/99 | DF10 (43) + 77A4 (42) | 0 | 1 % |
| 3 | N1 r02 | 48/48 (4 refused: delivery table full) | DF10 (95) | 119 | **48 %** |

- **Data is delivered 100 %, within about 30 s,** in every run. Only ACKs are lost. Resolved sends are ACKed 99–100 %, and at most one failed after retries.
- **The sink drops ACKs:** `Failed to queue ACK …: Queue full for slot type TX`. The TX queue holds 10 messages, shared by data, forwards and ACKs, and `EnqueueAck` drops when it is full. Senders then retransmit (timeouts 140–400 s, 3 retries), which adds load on the sink, so many sends are still retrying when the window ends.
- **The load on the busiest sink decides it, not the per-node slot count.**
  - Sink load vs ACK drops: Spearman ρ = 0.94 (p = 0.005, n = 6).
  - The app sends to one or two sinks, and the split varies per run.
  - With 3 slots the network copes when the traffic splits (43/42) and fails when one sink takes 95 of 100 messages: about 5–6 ACKs per superframe against 3 TX slots.
- **The earlier 0.47 ACK ratio** (lm200-eval) was these in-flight sends counted as failures.
- **2 vs 3 data slots per node, three runs each:**
  - not significant per run (ACK ratio inside the window 0.75 vs 0.82, p = 0.73; in-flight fraction p = 0.67);
  - ACK RTT per message falls from 186 s to 136 s (p ≈ 4e-9, clustered);
  - both are dominated by how the traffic splits across sinks.

---

## 9. Group messaging (FIX) and NM failover (SLP)

**Group messaging, N2 (FIX, 16 nodes, one sender 7B6C, 20 messages): FAIL, 0 % received.** No crashes, reboots or stack/heap problems.
- **The sender transmitted every message** (`PKT_TX dst=0xFF00 type=0x14` ×20), and every other node heard 7B6C's beacons and routing messages. No node received a single group frame, not even at the radio level.
- **Correction (2026-10-13, after re-reading the logs):** the first version of this report said 7B6C missed a slot shift. It did not. 7B6C and C270 shifted together at both growths: data slots 19/20 → 20/21 → 21/22, control slots 6/7 → 7/8 → 8/9. Their control indices are adjacent, not duplicated.
- **What the logs show** (manager 006C, times CEST):
  - From 06:47, neighbours' advertised quality for 7B6C flaps between ~137 and 0 (`Remote link quality from … for us: 0`), although they hear each other well.
  - 06:52:23: 006C reroutes 7B6C via 1484 at 3 hops (`uni=1 remote_q=0`), while still receiving 7B6C directly at RSSI −44 dBm, SNR 11. From then on it logs `Ignoring … routes from 0x7B6C: not an active direct neighbour`.
  - That 3-hop route raises max_hops to 3. At 06:52:46 the superframe grows 37 → 40 and the new slot table has no RX entry for 7B6C (data slots go only to direct neighbours). The growth is a consequence, not the cause.
  - The group burst (06:56:54–07:10:47) falls entirely in that period, so 7B6C transmitted in a slot nobody listened to.
- **Open question:** why neighbours advertise quality 0 for a strong direct link. Candidates: a partial routing-table update that omits a node being read as quality 0, or the link-stats expected/received bookkeeping.
- **The same test passed in lm200-eval** (764b893, 2026-10-09, same sender, 98.75 %). That run had 13 nodes; here the remote cluster joins during warmup, so the superframe grows.
- **Why it matters beyond group messaging:** a member with a good direct link can be marked one-way and lose its data slot everywhere, silently, while it keeps transmitting.
- **Repeat, N2b: 93.3 % received** (threshold 0.9), 0 duplicates, group ACK completeness 6.7 % (informational, as in lm200-eval). Members got 20/20, except 1484, 2A9C, 2AD4, DD3C and E464 (17/20). So the fault is **intermittent: 1 of 2 runs**. It is worth a library issue with the N2 logs (`runs/lm200s_features__fix/*-042549-group_SF9-r01`, nodes 7B6C, C270, 006C and 1484 between 06:45 and 07:05; excerpt bundle `runs/lm200s_features__fix/N2_excerpt_0645-0705.tar.gz`).

**NM failover with a pinned manager, N3 (SLP): PASS.**
- The manager 006C was restarted 3 times. It was back as manager 3/3, worst 2.7 s, with one manager throughout.
- 0 crashes and 0 unexpected reboots.
- Stack min free 1184 B (SuperframeUpdate); heap ≥ 154 KB with a flat slope.
- This was the first failover test on the member-sleep branch.

---

## 10. Statistics summary (`report/stats.md`)

Welch's t-test (two-sided) per run as the primary test. Per-message, per-node and per-cycle samples are secondary, because they are clustered within runs. TOST equivalence uses the gate's own margin.

| Comparison | Result |
|---|---|
| SF9 FIX vs SLP: PDR, convergence, latency | No difference (p ≥ 0.17) |
| SF12 FIX vs SLP: convergence (n = 4/4) | +149 s, **p = 0.21, not significant** (n = 3 had p = 0.037); equivalence not shown |
| SF12: last join, retries, PDR, latency | No difference |
| Stop/Start rejoin, S3 vs S4 | **Equivalent** within ±1 superframe |
| Formation, main cluster | No difference in join time; retries p = 0.064 |
| Sleep: PDR control vs deep | **Equivalent** within ±0.05 |
| Sleep: latency from schedule | Light = control (p = 0.87); deep +4 s (p = 0.074) |
| Reliable: what drives ACK loss | Busiest-sink load vs ACK drops ρ = 0.94 (p = 0.005); 2 vs 3 slots per node not significant per run (p = 0.73, n = 3/3) |

---

## 11. Data quality and testbed incidents

| Incident | Effect | Fix |
|---|---|---|
| GW-6 (Pi, 906 MB) hung during the first prewarm | S8/S9 delayed until the user power-cycled it | Prewarm ran `pio run` without `-j` (all 4 cores; one job already peaks at about 550 MB). Fixed in dcac0d1; `GW_PIO_JOBS[GW-6]=1` |
| `deploy.sh status` / `run-remote` returned 0 on failure; GW-4 GitHub TLS drops | Lost smoke attempts | Retries, `SKIP_GWS`, return codes (aaa1dfa … b34712b) |
| Orphan run dir after a manual stop | G0-S1 failed once, re-run (about 1.6 h) | Rename orphans before resuming |
| ~50 s with no serial capture after each reset (`deploy.sh reset` → `deploy.sh monitor`) | At SF7 the `state changed to 3` lines fell into the gap: formation saw 6/16 and 9/16 joined instead of 13 | Analysis counts joins from any state-3 evidence (01cda62); join times < 60 s after reset stay blurred |
| Gate cache keyed on run dir + `SUMMARY_VERSION` only | Re-gating served old results | Version bumped (78c965a); bump it with every summary change |
| Deep-sleep log line cut by the sleep itself | One false "unexpected reboot" | Resume line confirms the wake (9afc965) |
| `deploy.sh clean` never deletes plain `*.log` (find precedence) | – | Left unfixed on purpose: 167 April–July run dirs exist **only** on the gateways; archive them first |

The gateway disks are 10–50 % used. None is near full.

---

## 12. Recommendations

**Release / library**
1. **Ship 2.0.0 with the Stop/Start fix.** Close #112 with the hardware evidence (§5).
2. **Merge member-sleep after:**
   - **(a) Deep-sleep break-even.**
     - Make `min_sleep_ms` default to a value derived from `boot_time_ms` (about 4.5 s of active time per wake against 0.8 mA light sleep gives a break-even of ≈ 280 s today), or document it clearly.
     - Recommend light sleep for windows under a few minutes.
     - Add a startup check that warns when `min_sleep_ms` < boot + guard, like the one that hid deep sleep in this campaign.
   - **(b) SF12 convergence and join retries.** Neither is significant: SF12 +1.2 superframes, p = 0.21 (n = 4); formation join retries +5.7 per run, p = 0.064 (n = 3). Both lean the same way, so before making member-sleep the default:
     - run 4 or more extra interleaved SF12 pairs;
     - review the routing-advert and join-backoff changes between `ab9449a` and `86a4072`.
3. **Reliable delivery (both arms):**
   - give ACKs priority in the TX queue, or reserve room for them (today they are dropped when the queue is full);
   - let a sink request data slots according to its ACK load;
   - document that a sink needs about (messages it receives per superframe) TX slots; a uniform `lora_default_data_slots` can't cover a sink that takes most of the traffic (N1 r02: 3 slots, 48 % in flight).
4. **Group-slot loss (N2): direct link misclassified as one-way.** Find why neighbours advertise remote quality 0 for a strong direct link, which makes the manager drop the node's data-slot RX entry:
   - check how a partial or paginated routing-table update sets remote quality for nodes it doesn't list;
   - never drop the RX entry of a node that is still received directly and regularly;
   - add a native regression test: a node with a good bidirectional link must keep its data slot while the superframe grows and members join.
5. **Stack sizes:** fine after #131 (worst free 1200–2536 B).

**Application / firmware build**
6. **Lower the ESP-IDF log level on deep-sleep builds.** VERBOSE costs about 1.7 s per wake; at INFO or WARN, boot should drop to about 1.5 s. Then lower `LORA_DEEP_SLEEP_BOOT_MS` and re-measure.
7. **GPS off on the T-Beam when not needed.** At about 37 mA it dwarfs every sleep saving on this board.
8. **Measure energy.** All energy figures here are modelled. 15 minutes on a USB power meter per mode would calibrate `energy_params.yaml`.

**Testbed**
9. **Capture serial from the reset onward.** Hand the port from `gw-reset.sh` straight to the monitor, or start the monitor before the pulse, before trusting any join time under about 60 s.
10. **Reliable cells:** a cooldown at least as long as the worst-case RTT (about 7 min), so outcomes resolve; report in-flight sends separately.
11. **More repeats:**
    - keep ≥ 3 runs per arm for any A/B convergence claim (n = 1 produced a false blocker, and n = 3 a false "significant");
    - SF9 A/B with 2 runs per arm can't show equivalence, so use 3 or more.
12. **Archive the old gateway logs, then fix `deploy.sh clean`.** 167 April–July run dirs are on the gateways only.
13. **GW-6:** keep at 1 build job. Consider a Pi with more RAM, or swap on an SSD.
