# LoRaMesher 2.0.0 — PR #136 hardware round (fix for campaign finding #8)

**Recommendation: GO — merge #136 into release/2.0.0.**

The group cell that failed as N2 (0 %) passed in all three runs (95.4 / 100 / 99.2 %). The routing
slice now goes out whole: 0 routing TX skips in 5 of 7 runs and 1 (retried and sent at the next control
slot) in each of the other 2, against 93–156 per run on release/2.0.0. False `[UNIDIRECTIONAL]` marks at SF9 fell
from 280–739 per run to 0–9. The SF9 and SF12 13-node cells match or beat the campaign FIX numbers on
every metric; the SF12 slice stays at 3 entries. No reboots, crashes or stack regressions.

Two observations do not block the merge (§6): the group misses that remain are relay TX-queue
overflow under the reliable-group retry load (seen on the old firmware in N2b too), and at SF12 the
3-entry rotation still produces ~300 one-way marks per run, unchanged from release/2.0.0.

## 1. Setup

| | |
|---|---|
| Firmware | FIX-136 = env `ttgo-t-beam-v2`, LoRaMesher PR #136 `fix/routing-slice-airtime` @ 1f535fe (on release/2.0.0 ab9449a), RadioLib 7.7.1, lm200 configs, DEBUG |
| Dependency graph | Checked in every upload log, every stage: 16/16 boards `LoRaMesher @ 2.0.0+sha.1f535fe`, `RadioLib @ 7.7.1` (the campaign's `library.json` probe prints 7.8.1; the build graph is authoritative, as in the sleep campaign) |
| LoRaChat | lm200-sleep @ ce50cad (`[base_v2]` pinned to 1f535fe), `campaign_fix136.yaml` |
| Baseline | Sleep campaign FIX arm (release/2.0.0 ab9449a): `runs/lm200s_13node__fix`, `runs/lm200s_features__fix` (N2 = 042549, N2b = 064142) |
| Window | 2026-10-13 15:02Z – 2026-10-14 00:08Z, 9.1 h. SLP arm not run. |

## 2. Summary

| Stage | Cell | Runs | Gate | Result | Campaign FIX (release/2.0.0) | FIX-136 |
|---|---|---|---|---|---|---|
| F1 | group_SF9, sender 7B6C, 20 msgs | 3 | g2: ≥ 90 % in every run | **PASS** | N2 0 %, N2b 93.3 % | 95.4 / 100 / 99.2 % |
| F2 | 13node SF9_d63000 (S1) | 2 | g0 | GREY¹ | PDR 0.992 / 0.969, conv 375 / 344 s, p95 33.2 / 33.1 s | PDR 1.000 / 0.992, conv 354 / 304 s, p95 29.4 / 33.0 s |
| F3 | 13node SF12_d308000 (S11) | 2 | g1 | GREY² | PDR 1.0 ×4, conv 1354 / 1202 / 1238 / 1344 s, p95 122.2 / 117.1 / 120.1 / 122.8 s | PDR 1.0 / 1.0, conv 1222 / 1212 s, p95 119.9 / 118.0 s |
| F3 | ab vs campaign FIX (SF9 + SF12) | – | ab | **PASS** 6/6 | | |
| F4 | reliable dds3 (optional) | 0 | – | skipped | | budget: 9.1 h + 1.1 h > 10 h |

¹ The only GREY check is the superframe size (34 / 37 slots), because there is no July reference at SF9. The campaign's FIX runs on this cell were GREY on the same check (37). The other 24 checks passed.
² `fair_pdr` has no July baseline for these cells (values 1.0 and 0.996). The other 5 checks passed.

**Welch (two-sided, per run; same method as stats.md):**

| Cell | Metric | n FIX / FIX-136 | FIX | FIX-136 | Δ | 95 % CI | p | Verdict |
|---|---|---|---|---|---|---|---|---|
| group SF9 | received ratio | 2 / 3 | 0.467 | 0.982 | +0.515 | [−5.39, 6.42] | 0.47 | n.s. (N2/N2b variance) |
| SF9 | fair PDR | 2 / 2 | 0.981 | 0.996 | +0.015 | [−0.087, 0.117] | 0.40 | n.s. |
| SF9 | PDR per sender (paired, 10) | | | | +0.015 | [0.001, 0.030] | **0.037** | better |
| SF9 | convergence | 2 / 2 | 359 s | 329 s | −30 s | [−184, 124] | 0.44 | n.s. |
| SF9 | latency p95 | 2 / 2 | 33.2 s | 31.2 s | −1.9 s | [−24.7, 20.9] | 0.48 | n.s. |
| SF12 | fair PDR | 4 / 2 | 1.000 | 1.000 | 0 | – | – | equivalent (TOST ±0.02) |
| SF12 | convergence | 4 / 2 | 1284 s | 1217 s | −67 s | [−187, 52] | 0.18 | n.s. |
| SF12 | join retries | 4 / 2 | 17 | 13 | −4 | [−8.9, 0.9] | 0.08 | n.s. |
| SF12 | latency p95 | 4 / 2 | 120.6 s | 118.9 s | −1.6 s | [−6.3, 3.0] | 0.38 | n.s. |

With n = 2–3, nothing per run is significant either way, and equivalence (TOST) is not shown except
for SF12 PDR. Every point estimate moves in the right direction. The group gate is the
pre-registered test, and it passed in every run.

## 3. Routing slice evidence

Counted from `PKT_TX … type=0x32 size=`, `Created routing table message … entry count`, and
`TX skip:` lines (a skip within 2 s of a Created line counts as a routing-message skip). Columns:

- **Full slices sent:** the range over nodes of full-size slices actually transmitted.
- **Tail run:** the longest run of routing messages *sent* without a full slice, counted from the first full slice.
- **UNI:** `[UNIDIRECTIONAL]` marks in the measurement window, then how many of those were on links where both ends received each other's routing within ±120 s.
- **Relay-while-direct:** `Route updated hops>1` while the destination was heard directly (±60 s), in window.

| Run | Slice (entries / B) | Full slices sent | Routing skips (retried + sent) | Tail run | UNI (all / both hear) | Relay-while-direct | ≥ 3 hops | Manager max_hops |
|---|---|---|---|---|---|---|---|---|
| N2 (FIX) | 10 / 113 | 1–32 | **156** (3) | **29** (DD3C) | 749 / **739** | 66 | 9 | 1–3, sf 35–40 |
| N2b (FIX) | 10 / 113 | 0–31 | 119 (1) | 28 (77A4) | 626 / 600 | 38 | 8 | 1–3, sf 34–40 |
| F1 r01 | 9 / 103 | 32–35 | 0 | 1 | 18 / 3 | 20 | 0 | 1, sf 34 |
| F1 r02 | 9 / 103 | 24–33 | 0 | 1 | 15 / 8 | 22 | 0 | 1–2, sf 34–37³ |
| F1 r03 | 9 / 103 | 30–34 | 1 (1) | 1 | 0 / 0 | 14 | 0 | 1–2, sf 34–37 |
| S1 FIX r01 | 10 / 113 | 0–32 | 93 (1) | 10 (C270) | 322 / 280 | 87 | 0 | 2–4, sf 37–43 |
| S1 FIX r02 | 10 / 113 | 4–32 | 154 (10) | 15 (E464) | 323 / 295 | 86 | 3 | 1–3, sf 34–40 |
| F2 r01 | 9 / 103 | 32–38 | 1 (1) | 1 | 14 / 7 | 18 | 0 | 1 (2 for 30 s in formation), sf 34 |
| F2 r02 | 9 / 103 | 31–33 | 0 | 1 | 84 / 9 | 14 | 0 | 1→2 from 22:59, sf 37³ |
| S11 FIX ×4 | 3 / 43 | 24–34 | 0 | 1 | 296–319 / all | 229–291 | 2–4 | 1–3, sf 34–40 |
| F3 r01 | 3 / 43 | 27–34 | 0 | 0 | 305 / 305 | 269 | 3 | 1–2, sf 33–37 |
| F3 r02 | 3 / 43 | 27–34 | 0 | 0 | 328 / 328 | 291 | 3 | 1–2, sf 34–37 |

³ Depth 2 comes from the marginal 2AD4 ↔ 006C link. 2AD4 hears 006C at SNR −7 to −11 dB and
received 4 of 30 routing messages. The manager reroutes 2AD4 via a neighbour; in F1 r02 the direct
route came back at 19:39:47. The campaign's FIX r02 also ended at 37 slots. This is correct
behaviour for a marginal link, not #136.

**Brief checks:**

- **SF9 slices:** every node sends full 9-entry, 103 B slices 24–38 times per run, alternating with a
  5–6-entry tail (63–73 B), or a 3-entry tail in the 16-node group cell. No node sends only tails, and
  no node sent two tails in a row after its first full slice. On release/2.0.0 the slice was 10
  entries / 113 B. It did not fit the 950 ms slot after the guard, so it was skipped 93–156 times per run. Four
  nodes in N2 (7B6C, 2A9C, B4DC, DD3C) almost never sent a full slice, and tail runs reached 29 slices
  (about 16 min).
- **TX skips on routing messages:** 2 in 7 runs (F1 r03 DF10, F2 r01 E464), both retried with the *same* slice and sent at the next
  control slot (e.g. E464 in F2 r01: 9 entries skipped, the same 9 entries sent 27.5 s later). The
  rotation no longer advances on a skip.
- **SF12 slices stay at 3 entries / 43 B** on every node in both runs (0 skips), the same as
  release/2.0.0.
- **[UNIDIRECTIONAL] where both ends hear each other:** SF9 0–9 per run (FIX: 280–739). SF12 is
  unchanged at about 300 (see §6.2).
- **Relay routes while direct:** SF9 14–22 per run (FIX: 38–87); all are 2-hop. SF12 is unchanged.
- **3–4-hop routes inside the main cluster:** none in the measurement window at SF9 (FIX: up to 9).
  SF12 has 3 per run (FIX: 2–4); they come from the SF12 one-way marks.

## 4. Group cell detail (F1)

| Run | Received | Members missing | Sender data slot on members (RX / not RX prints, in window) |
|---|---|---|---|
| r01 | 95.4 % (229/240) | 1484, 2A9C, DF10: seq 15, 17, 18; 2AD4: seq 17, 18 | 10 / 1 |
| r02 | 100 % | – | 37 / 2 |
| r03 | 99.2 % (238/240) | 1484, 2AD4: seq 19 | 7 / 3 |

The other 8 members got 20/20 in every run. The sender's data slot stays RX on members in 54 of 60
slot-table prints. The 6 prints without it are on 1484, 2A9C and 2AD4, the members that do not hear
7B6C reliably. That is #136's rule (keep RX for nodes heard directly in the last 3 superframes)
working as designed. None of those prints coincides with a missed sequence number's direct
transmission. In N2, by contrast, DF10 marked 7B6C UNIDIRECTIONAL and the group traffic stopped. In
F1 r01 DF10 marked 7B6C at 18:40:29, kept `RX:#18(7B6C)` and missed only the 3 sequence numbers below.

**r01 miss: log evidence.** Bundle `runs/lm200s_features__fix136/F1r01_excerpt_1845-1905.tar.gz`
contains the sender 7B6C, the manager 006C, the affected members 1484, 2A9C, DF10 and 2AD4, the relay
7984, and config.yaml, from 18:45 to 19:05 CEST, with a README.

- 7B6C sends one group message per superframe from its single data slot. The direct neighbours 006C
  and 7984 receive seq 13–19 in order: seq 15 at 18:54:09, 17 at 18:55:46, 18 at 18:56:18.
- 4–8 s after each new sequence number, the relays log
  `Failed to queue forwarded DATA: Queue full for slot type TX`. 7984 logs it after seq 15, 16, 17 and
  18 (20 times in the run), 006C after 15 and 16 (8 times). The 10-entry TX queue is full of forwarded
  copies of older sequence numbers and of the reliable-group retransmissions. One data slot per
  superframe cannot drain it.
- 1484, 2A9C and DF10 only receive relayed copies, and those arrive late and out of order: seq 13 at
  18:55:49, 16 at 18:58:04, 14 at 18:58:32, 19 at 19:02:14. They also get dozens of
  `Dropping duplicate GROUP … seq=7/9/10`. Seq 15, 17 and 18 were dropped at the relays and never arrive.
- From seq 8 on, 7B6C's ACKs arrive after the 120 s window (`matched=0`, `acks=0`), so every message is
  retried. That retry load feeds the relay backlog.

**r03 miss:** no forward drops. Seq 19 was sent at 20:13:56 and was still queued at the relays
when the window closed at 20:19:47. 1484 and 2AD4 got seq 18 four minutes after it was sent.

N2b (release/2.0.0, 93.3 %) had the same pattern: 6 members missing seq 14/15/18. This is the
reliable-group retry and forward load against single data slots, not routing. It belongs with the
open item "reliable sink ACK drops", not with #136.

## 5. Health

| | F1 | F2 | F3 | Campaign FIX |
|---|---|---|---|---|
| Unexpected resets / fatal lines | 0 | 0 | 0 | 0 |
| Min free stack RadioEvent / LoRaMeshMain / SuperframeUpdate (B) | 1336 / 2488 / 1200 | 1328 / 2520 / 1208 | 1304 / 2528 / 1208 | 1336 / 2528 / 1184–1280 |
| CRC check failed (per run) | – | 305 / 325 | 28 / 37 | SF9 206–260, SF12 12–20 |

New WARNING/ERROR lines compared with the campaign:

- **Group cell:** `Queue for slot type TX is full (size=10)` (67–79 per run), `Failed to queue forwarded DATA`
  and `Failed to queue ACK`. These come from the group backlog in §4, not from routing.
- **SF12, 006C only:** `Invalid message type: 0` → `Failed to deserialize message header`, 44 in F3 r01
  and 24 in F3 r02. Campaign SF12 runs: 0.
  - Only the long-link node 006C logs them.
  - They fall in every owner's data slot and in discovery slots, at random offsets of 0.0–2.6 s into the slot. An offset of 0.0 s is too early for any in-slot SF12 frame.
  - The inactive remote nodes never start their radio.
  - #136 touches neither `base_header.cpp` nor the RX path; its `lora_mesh_protocol.cpp` change is a refactor of `CanFitInSlot`.
  - A malformed frame sent by a mesh node would also be logged by the other 12 nodes.
  - 006C has logged the same error on release/2.0.0 as well (2–5 per SF7 formation run).
  - Reading: frames only the long-link node decodes (foreign traffic or noise that passes the CRC), with a higher rate on the night of the run. PDR was 1.0 in both runs. Not attributed to #136, but worth one look on the next SF12 run.

## 6. Open observations (not blocking)

1. **Reliable group under backlog (§4).** Single data slots plus 120 s ACK windows make every
   message retry. The resulting duplicates fill the relays' 10-entry TX queues, and new sequence numbers
   are dropped. The fix options are app or library: back off retries when the TX queue is
   ≥ the data slots, prefer new sequence numbers over duplicate forwards, or give the sender 2 data
   slots. Present on release/2.0.0 (N2b).
2. **SF12 one-way marks (unchanged from release/2.0.0).** With 3-entry slices and a 12–15-entry
   table, a neighbour's entry is absent from about 4 of every 5 slices. About 300 `[UNIDIRECTIONAL]`
   marks and about 270 relay-while-direct routes per run, in both arms. The ±120 s "both hear" window is one SF12
   superframe, so that check is weak at SF12. PDR is 1.0, but this is the same mechanism #136 removed at
   SF9. `remote_absent_threshold` could be scaled by the rotation length (entries / slice capacity) in
   a follow-up.

## 7. Testbed incidents

- **GW-6 prewarm took 68 min** (cold build cache). No effect on runs.
- **GW-4 `pio pkg update` hung for 93 min** at 0 % CPU during F2's gateway upgrade. The other gateways
  finished in under 30 s. The MTU is already pinned to 1400 (`fix-gw4-mtu.sh`), so it was not the known MTU black hole.
  Killing the process (PID 1225255) let the built-in retry finish, and F2 started at 20:00Z instead of 18:23Z. This cost the
  optional F4.
- Pre-run clock skew of 2.6–4.1 s between gateways (warned, not corrected). Same range as the campaign's
  FIX runs (2.2–4.5 s), so the latency comparison is like-for-like.
- The campaign summary line says `finished: UNKNOWN` because the gate set of this round does not map to
  the sleep campaign's overall verdict. Per-stage gates are in `runs/lm200_fix136/gates/`.

## 8. Reproduce

```
python3 scripts/testbed/analysis/routing_slice_checks.py runs/lm200s_features__fix136/*group* --group
python3 scripts/testbed/analysis/routing_slice_checks.py runs/lm200s_13node__fix136/* runs/lm200s_13node__fix/*SF12* runs/lm200s_13node__fix/*SF9*
python3 scripts/testbed/analysis/stats_compare.py --fix136      # -> runs/lm200_fix136/report/stats.md
```
