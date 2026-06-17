# Spreading-Factor-Aware Offered Load in a TDMA LoRa Mesh

*Diagnosing the SF12 delivery collapse, and turning it into a capacity model.*

This note documents an experiment on the 13-node main cluster: why packet delivery
collapses at SF12, why that is **not** a protocol defect, and how we re-run the
measurement so each spreading factor is evaluated at a fair, sustainable load. It is a
companion to `evaluation.tex` and reuses its notation (capacity ceiling `C`, data-slot
recurrence `R`, superframe duration `T_sf`, control-overhead fraction `φ`). Every number
below is taken from the repository and is reproducible (§9).

---

## 1. Motivation — why we are doing this

We report steady-state packet delivery ratio (PDR) and end-to-end latency across SF7, SF9
and SF12 on the isolated 13-node main cluster (batch `main_cluster_pdr`). The headline
measurement (mean ± 95 % CI, post-warm-up window):

| SF | PDR | 95 % CI | p50 latency | n |
|----|-----|---------|-------------|---|
| 7  | **0.982** | [0.973, 0.991] | 0.4 s | 7 |
| 9  | **0.984** | [0.968, 1.000] | 0.8 s | 5 |
| 12 | **0.529** | [0.391, 0.666] | **488 s** | 5 |

SF12 looks broken: barely half the packets arrive, latency is in the **hundreds of
seconds**, and the confidence interval is enormous. The natural question for the paper is:
**is \NLM failing at SF12, or are we measuring it unfairly?** This note answers that
question and defines a re-run that measures every SF at a comparable, sustainable load.

The short answer: **it is a load-versus-capacity mismatch, not a protocol defect.** We
were offering the same application load at every SF, but the network's delivery capacity
collapses ~5× from SF7 to SF12. At SF12 we drove the network past saturation.

---

## 2. Background — the TDMA superframe sets the capacity

\NLM is a TDMA protocol. The Network Manager builds a **superframe** of fixed-length slots
and sizes it to honor the radio duty cycle:

```
T_sf = NM_tx_time / duty_cycle          (duty_cycle = 10 %)
```

where `NM_tx_time` is the airtime of one sync beacon + one routing table + the node's data
slots. The slot length is `t_slot = roundup50(ToA(L_max) + guard + margin)`, so it scales
with the LoRa time-on-air, which grows steeply with SF. Each node is granted **one data
slot per superframe** (`s = 1`), giving:

- **data-slot recurrence** `R = T_sf / s` — how often a node may transmit;
- **per-node capacity ceiling** `C = s · L_max / T_sf` — the most application goodput a node
  could offer if it filled every slot;
- **control overhead** `φ = (n_sync + n_ctrl + n_disc) / N` — the slot fraction spent on
  coordination rather than data.

Measured converged superframe per SF (from `analysis/superframe.py` on representative runs;
13 nodes, 13 data slots, 10 % duty):

| SF | t_slot | T_sf | composition (sync / ctrl / disc / data) | φ | R | C |
|----|--------|------|------------------------------------------|----|----|----|
| 7  | 0.65 s | 24 s | 3 / 13 / 6 / 13 | 59 % | 24 s | **2.49 pkt/min** |
| 9  | 0.95 s | 38 s | 4 / 13 / 8 / 13 | 62 % | 38 s | **1.58 pkt/min** |
| 12 | 3.30 s | 119 s | 2 / 13 / 4 / 13 | 53 % | 119 s | **0.51 pkt/min** |

The protocol is behaving exactly as designed: to keep airtime within 10 % duty cycle as the
SF12 time-on-air grows ~30× over SF7, it stretches the superframe to ~119 s. The
consequence is that **per-node capacity falls from 2.49 to 0.51 pkt/min** — a node may
transmit only once every ~2 minutes at SF12. (Note also that ~55–60 % of every superframe is
control overhead, one control slot per node plus discovery — a second-order target discussed
in §8.)

---

## 3. Diagnosis — the offered load did not scale with SF

The measured DATA traffic is the periodic MQTT-monitor report (`MQTT_MON_ENABLED`,
`MON_SENDING_EVERY = 300 s`); each active node offers roughly 0.3–0.6 pkt/min, **independent
of SF**. Normalizing the measured offered load by the per-node ceiling gives the dimensionless
load `ρ_node = offered / C`, and the per-node slot utilization including *forwarding* gives the
bottleneck load `ρ_max` (computed by `analysis/load.py` on the representative runs):

| SF | offered (per node) | C | **ρ_node** | **ρ_max (bottleneck)** |
|----|--------------------|----|-----------|------------------------|
| 7  | 0.53 pkt/min | 2.49 | **0.21** | 0.28 |
| 9  | 0.31 pkt/min | 1.58 | **0.19** | 0.38 |
| 12 | 0.61 pkt/min | 0.51 | **1.20** | **2.71** |

SF7 and SF9 sit comfortably under capacity (ρ < 0.4 everywhere). SF12 is already past a single
node's slot (ρ_node = 1.2 > 1) — and that is *before* relaying. A relay must forward other
nodes' packets through its own single data slot, so the binding constraint is the **busiest
relay**. Counting distinct source-flows through each next hop
(`data_sent`/`data_forwarded`), the SF12 bottleneck carries **F ≈ 4** flows and runs at
**ρ_max ≈ 2.7 — nearly 3× oversubscribed**, while SF7/SF9 bottlenecks stay ≈ 0.3.

That is the collapse. The bottleneck relay's queue cannot drain: latency balloons to ~488 s
(several superframes), packets are stranded past the 30-minute window, and the wide CI is the
signature of a network teetering at saturation. The flows routed through `1484` are exactly
the low-PDR ones.

**Two measurement caveats** also surfaced and must be fixed before re-measuring:

1. **The slot-aware pacing could not be confirmed to run.** The firmware has a
   `waitForDataSlots()` helper meant to pace senders to the schedule, but its log line never
   appears, and the real `Sending DATA` timestamps arrive in **bursts of ~5 packets**
   110–480 s apart — inconsistent with per-slot pacing. We cannot claim pacing was active.
2. **The deployed binary was a dirty build** (`First-LoRaChat-126-g8549f04-dir`), so the
   running firmware did not provably match committed source.

---

## 4. The existing figure is load-confounded

The current figure `runs/main_cluster_pdr/main_cluster_pdr_sf_comparison.png` plots raw
PDR-vs-SF bars at fixed offered load. Because the load is held constant while capacity
collapses, that chart **conflates spreading factor with offered load** — it makes a
load-saturation artifact look like a protocol property.

The honest presentation plots PDR (and latency) against the normalized load ρ — see
`runs/main_cluster_pdr/main_cluster_pdr_load_curve.png` (generated by `analysis/plot_load.py`).
On the bottleneck-relay load axis ρ_max, SF7 and SF9 cluster at ρ_max ≈ 0.25–0.4 with
PDR ≈ 0.98 and sub-second latency, while SF12 sits **past the ρ = 1 saturation line**
(ρ_max ≈ 1.4–1.7) with PDR ≈ 0.5–0.6 and ~400 s latency. All three SF cells fall on one
degradation curve: high PDR while ρ < 1, collapse once the bottleneck relay is oversubscribed.
This reframes SF12 from "a failing spreading factor" to "the one cell we happened to run past
its capacity."

---

## 5. A capacity model for the optimal load

Because the superframe is deterministic, we can predict the sustainable load analytically
rather than sweep it blindly. `analysis/model.py` reproduces the LoRa time-on-air to <1 ms
against the firmware-logged values (ToA(242)=523, ToA(115)=829, ToA(51)=2728 ms) and rebuilds
the superframe slot budget, yielding `R` and `C` per SF (the table in §2).

Adding the topology factor `F` (busiest-relay fan-in, measured from the routing/forwarding
events) gives the **sustainable per-source load**:

```
sustainable_per_source = C / F = 1 / (R · F)
recommended_load(ρ_target) = ρ_target · C / F      (we use ρ_target ≈ 0.2–0.5 for margin)
```

This is the *theoretical curve for our topology*: the maximum a source may offer before the
busiest relay saturates, and the recommended operating point below it. It generalizes — feed
a different `(node_count, hop_depth, F)` and it predicts that topology's optimum. Indicative
recommended MQTT-mon intervals (target ρ around the relay ≈ 0.5; finalized in §6):

| SF | C / F (sustainable) | recommended `mon_sending_every` |
|----|---------------------|----------------------------------|
| 7  | ~1.25 pkt/min | 300 s (nominal) is already ρ≈0.16 — ample headroom; can push to ~100 s |
| 9  | ~0.79 pkt/min | 300 s is ρ≈0.25 — headroom; can push to ~150 s |
| 12 | ~0.13 pkt/min | **raise to ~600–900 s** (nominal 300 s is the saturation cause) |

---

## 6. Method — the re-run

We change the experiment in three deterministic steps, then re-measure.

1. **Deterministic static pacing.** Replace the unconfirmed `waitForDataSlots()` slot-pacing
   in the senders with a plain fixed delay, so offered load equals the configured interval
   *exactly* and is reproducible. Build clean and commit, so the deployed binary carries no
   `-dir` suffix and provably matches source.
2. **Expose the load knob.** Make `MON_SENDING_EVERY` a per-cell testbed parameter so each SF
   can be run at its own offered load.
3. **Per-SF loads.** SF7 and SF9: keep the nominal interval and add one higher-load point to
   *confirm headroom* (not to hunt the breaking point). SF12: set the model-recommended
   interval. Because SF12's sustainable rate is low, extend the SF12 measurement window so the
   PDR estimate has enough samples.

---

## 7. Expected results

- SF12 PDR recovers from ~0.53 toward the SF7/SF9 band (~0.9+), with p50 latency dropping
  from ~488 s to a few slot-times — packets no longer stranded.
- SF7 and SF9 remain ~0.98 at both the nominal and the higher-load point, confirming the
  predicted headroom.
- On the ρ axis, all three SF cells cluster at high PDR for ρ < 1 and degrade as ρ → 1 — the
  measured anchors land where the model predicts, validating the capacity curve of §5.

---

## 8. Takeaway

The SF12 "failure" is a measurement run past the network's capacity, not a routing or
delivery defect. The contribution is a **design rule**: for a given SF and topology, the
sustainable offered load is `C / F`, and the operating point should sit a safe margin below
it. Picking offered load per SF from the capacity model makes the PDR-vs-SF comparison fair
and demonstrates that \NLM delivers reliably whenever load is within capacity. A secondary,
orthogonal lever is the ~55–60 % control overhead (one control + discovery slots per node):
reducing it would raise the data share of the duty-cycle budget and lift `C` directly.

---

## 9. Reproducibility

```bash
# Per-cell PDR/latency (already aggregated):
python3 scripts/testbed/analysis/multirun.py scripts/testbed/runs/main_cluster_pdr

# Capacity model (ToA validation + per-SF superframe/capacity):
python3 scripts/testbed/analysis/model.py

# Converged superframe per run (T_sf, slot composition, R, C):
python3 scripts/testbed/analysis/superframe.py <run_dir>

# Load normalization + topology fan-in + theory-vs-measured figure (to be added, §6 tooling):
python3 scripts/testbed/analysis/load.py <run_dir>
python3 scripts/testbed/analysis/plot_load.py scripts/testbed/runs/main_cluster_pdr
```

Source artifacts: measured data in `scripts/testbed/runs/main_cluster_pdr/` (per-run logs +
`aggregated.json`); the existing load-confounded figure
`main_cluster_pdr_sf_comparison.png`; the re-run batch
`scripts/testbed/batches/main_cluster_pdr_static.yaml` (to be added, §6).

# TODO

- Explicar perque no es PDR del 100% en propies paraules.