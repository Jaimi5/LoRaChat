# Testbed analysis scripts

Post-processing for the LoRaMesher v2 testbed run batches under
`scripts/testbed/runs/<batch>/`. Each batch holds run subdirectories named
`<batch>-<YYYYMMDD-HHMMSS>-<cell>-r<RR>/` containing `config.yaml`,
`lifecycle.json`, and gzipped per-device logs in `logs/monitor-dev-*.log.gz`.

| Module | Role |
|--------|------|
| `parse_logs.py` | Low-level log reader (timestamp regex, `_open_log` gz/plain, ANSI strip). |
| `multirun.py` | Aggregate outcome metrics (PDR, latency, convergence, energy) per cell across reps → `aggregated.json`. `_cell_of()` parses `(cell, rep)` from a run-dir name. |
| `model.py` | Closed-form superframe model mirroring the firmware slot budget: `superframe()`, `metrics()` (capacity / overhead / recurrence / energy), `convergence_floor_s()`. |
| `superframe.py` | Extract the *converged* superframe structure from a run's logs (`extract()`), and aggregate measured anchors across batches (`aggregate_anchors()`). |
| `plot.py` | Per-SF outcome figures (formation comparison, with `--model` overlays). |
| `plot_superframe.py` | Single-batch superframe structure/capacity figures (SF-keyed). |
| `plot_paper.py` | **Multi-batch combiner** for the publication figure set (below). |
| `app_pdr.py` | **Version-neutral** app-layer end-to-end PDR / goodput / latency from `APP_TX`/`APP_RX`, measured *above* LoRaMesher so v1 and v2 are scored identically. Reports both raw `delivered/sent` and the fair `delivered/intended` (see below). |
| `plot_compare.py` | Overlay two campaigns (v1 vs v2) of the same batch on one figure set (energy + fair PDR + latency). |

## Paper figures (superframe / data-slot study)

`plot_paper.py` merges the measured anchors from **several** run batches into one
design-space picture, so the per-node *data-slot* axis — which `plot_superframe.py`
could only draw as a model curve with a single ds=1 anchor — is empirically
anchored at every data-slot count that was actually run (e.g. ds=1 from
`formation` plus ds=2,4 from `dataslots_check`).

### Pipeline

```
runs/<batch>/<run>/logs/monitor-dev-*.log.gz
        │  superframe.extract()        # parse converged NM slot-budget lines
        ▼
   per-run record  {sf, data_slots_granted, node_count, max_hops,
                    overhead_frac, capacity_Bps, recurrence_s, composition, …}
        │  superframe.aggregate_anchors(batch_dirs, drop_first=1)
        ▼
   anchors keyed by (sf, data_slots)   # mean ± n, full-membership-filtered
        │  model.metrics()  + matplotlib
        ▼
   runs/figures/superframe_*.{pdf,svg,png}
```

`aggregate_anchors()` keys by `(sf, round(data_slots_granted))` (reading the SF and
data-slot count from each run's own record, so cells named by the knob under test
— `Slots2`, `Slots4` — are handled), drops the `r00` warm-up, and by default keeps
only runs that reached full membership for their SF so every data-slot anchor is
compared at the same network size.

### Five output figures

1. `superframe_composition` — slot composition per SF (measured, ds=1): control
   overhead dominates the default frame.
2. `superframe_dataslots_composition` — composition vs data_slots at one SF
   (measured ds=1,2,4): the fixed control block, the growing data block, the
   diluting overhead.
3. `superframe_pareto` — **headline**: capacity vs latency model frontier per SF
   with the measured data-slot anchors walking the SF frontier.
4. `superframe_sensitivity` — capacity/overhead vs data_slots (measured-anchored)
   and capacity/energy vs duty cycle (model-only).
5. `superframe_validation` — model-predicted vs measured (capacity, overhead,
   superframe time, recurrence) at each measured data-slot count; the title prints
   the max relative error.

Every figure is written as **PDF + SVG** (vector, embedded TrueType fonts) and
**PNG**, with serif publication styling.

### Reproduce

```bash
# (optional) cache one run's superframe.json for inspection
python3 scripts/testbed/analysis/superframe.py \
    scripts/testbed/runs/dataslots_check__lmv2/dataslots_check-20260608-214103-Slots4-r01

# build the combined publication figure set into runs/figures/
python3 scripts/testbed/analysis/plot_paper.py \
    --batch scripts/testbed/runs/formation__lmv2 \
    --batch scripts/testbed/runs/dataslots_check__lmv2 \
    --out   scripts/testbed/runs/figures --sf 9
```

Flags: `--batch DIR` (repeatable, ≥1), `--out DIR` (created if absent),
`--sf N` (SF for the data-slot panels; default 9), `--drop-first N` (warm-up reps
to skip; default 1), `--keep-partial` (include runs that did not reach full
membership). Run `--help` for the full list.

The captions, methods, and results prose for these figures live in
`docs/paper/superframe_figures.tex` (with the regeneration command in its header).
To densify the measured data-slot axis, add more cells (e.g. `Slots3/6/8`) to
`scripts/testbed/batches/dataslots_check.yaml`; the combiner picks up any
data-slot value with no code change.

## Previous-LoRaMesher vs T-LoRaMesher comparison (`plot_compare.py`)

Compares the two protocols head-to-head under the sim load generator
(`batches/sim_load_compare*.yaml`, `sim_size_compare.yaml`). **Naming:** the `v1`
campaign/dirs are the **Previous LoRaMesher** (always-on RX); the `v2` campaign/dirs are
**T-LoRaMesher** (TDMA superframe) — the figures use those names, while `v1`/`v2` remain
the run-dir and metric-field shorthand here. The
firmware emits `APP_TX` at the originator and `APP_RX` at the final sink **above** the
mesh library, so the metric is version-neutral by construction — the internal v1/v2
mesh logs differ and are not used. `app_pdr.py` FIFO-matches each `APP_RX` to the
oldest unmatched `APP_TX` with the same `(src, seq)` and produces PDR, goodput, and
end-to-end latency. `multirun.py` rolls these up per cell; `plot_compare.py` overlays a
v1 campaign against a v2 campaign of the same batch.

### Fair PDR = delivered / intended (read this)

The obvious `PDR = delivered / sent` is **misleading** for the v1-vs-v2 comparison:
`sent` is the count of `APP_TX` lines, so a stack that offers less load (or whose sources
fail to join) gets a smaller denominator and a flattered PDR. The version-neutral score is

> **fair PDR = delivered / intended,  intended = packet_count × N_designed_senders**

where **`N_designed_senders`** is the count of *active, non-sink* source nodes read from
`config.yaml` — a fixed per-cell constant (13 for the full 16-node topology; 10 for
`sim_size_compare`, which marks the remote cluster `node_active: 0`). It is **not** the
per-run *observed* sender count: a source that never joins emits zero `APP_TX`, and keying
`intended` off the observed count would shrink the denominator and re-flatter that stack.
Scoring on the designed count charges un-joined sources as undelivered intended load.
Sinks are the WiFi-uplink gateways (`wifi_ssid != nowifi`: `77A4/DF10/E464`), cross-checked
behaviourally (they emit `APP_RX`, never `APP_TX`).

`app_pdr.py` reports `pdr_delivered_intended`, `n_designed_senders`, and the honesty
companion **`sender_participation = observed / designed`**. Concretely: on
`sim_load_compare_full` v1's remote cluster (3 of 13 sources) failed to join the network
in 3 of 4 cells (participation 10/13), so those cells' PDR is legitimately depressed —
disclosed by the participation panel, **not** hidden. See
`docs/paper/v1_v2_fulltopo_discussion.md` for the full confound analysis and what to
publish.

### Output figures (written to `--out`)

| Figure | Meaning |
|--------|---------|
| `app_pdr.png` | **Fair PDR = delivered / intended** on the designed source count; invalid cells shaded. |
| `sender_participation.png` | Fraction of designed sources that joined and sent — the honesty companion showing *why* v1's PDR differs. |
| `app_delivered_count.png` | Absolute packets delivered, v1 vs v2. |
| `energy_mean_current.png` | Mean radio current, v1 vs v2. |
| `energy_per_delivered_bit.png` | mJ per delivered application bit (equal-data normalizer). |
| `app_latency_p50.png` / `app_latency_p95.png` | End-to-end latency (the v2 TDMA cost). |
| `energy_sleep_crossover.png` | `model.py` projection of v2 mean current vs duty target, with the measured v1 floor overlaid. |

**Invalid-cell shading.** Cells that deliver 0 on *both* versions (payload > single-packet
MTU, no app fragmentation) or where a version's capture is incomplete (participation < 0.5)
are shaded grey and labelled — never read as a v1-vs-v2 outcome. `plot_compare.py` logs
each such cell.

### Independent audit (`audit_app_pdr.py`)

`analysis/audit_app_pdr.py --v1 <dir> --v2 <dir> --out <fig_dir>` re-derives every number
straight from the raw `APP_TX`/`APP_RX` lines (it does **not** import `app_pdr.py`), prints
per-sender sent/delivered, the per-sink `APP_RX` reconciliation (`delivered == Σ sinks`,
PASS/FAIL per cell), the invalid-cell flags, and writes `audit_<campaign>.csv`
(one row per version×cell×sender) for hand-inspection. Use it to verify the figures.

### Reproduce

Match `--nodes`/`--max-hops` to each batch's topology (13-node cluster → 13/3; full
16-node topology incl. the long link → 16/5) so the crossover model projection is right.
Use `--x size` for the packet-size sweep so size cells key correctly.

```bash
cd scripts/testbed

# Re-aggregate every campaign that feeds a figure set (populates app_pdr_fair).
for d in sim_load_compare_prev sim_load_compare_v2 \
         sim_load_compare_full_v1 sim_load_compare_full_v2 \
         sim_size_compare_v1 sim_size_compare_v2; do
  python3 analysis/multirun.py runs/$d
done

# energy  (13-node cluster, x = load)
python3 analysis/plot_compare.py \
    --v1 runs/sim_load_compare_prev --v2 runs/sim_load_compare_v2 \
    --nodes 13 --max-hops 3 --out ../../docs/paper/figures/energy

# sim_load_compare_full  (16-node topology incl. long link, x = load; limitations caption)
python3 analysis/plot_compare.py \
    --v1 runs/sim_load_compare_full_v1 --v2 runs/sim_load_compare_full_v2 \
    --nodes 16 --max-hops 5 --out ../../docs/paper/figures/sim_load_compare_full \
    --caption "v1/v2 not interleaved (v1 overnight, v2 afternoon); 3.5 km long link is meteorologically marginal — v1's remote cluster failed to join in 3 of 4 cells (see participation panel)."

# sim_size_compare  (packet-size sweep, x = size)
python3 analysis/plot_compare.py \
    --v1 runs/sim_size_compare_v1 --v2 runs/sim_size_compare_v2 \
    --x size --out ../../docs/paper/figures/sim_size_compare

# Independent audit (raw-log reconciliation + CSV) for each campaign:
python3 analysis/audit_app_pdr.py --v1 runs/sim_load_compare_full_v1 \
    --v2 runs/sim_load_compare_full_v2 --out ../../docs/paper/figures/sim_load_compare_full
python3 analysis/audit_app_pdr.py --v1 runs/sim_size_compare_v1 \
    --v2 runs/sim_size_compare_v2 --out ../../docs/paper/figures/sim_size_compare
```

> **Confounds (see `docs/paper/v1_v2_fulltopo_discussion.md`).** On `sim_load_compare_full`
> the 3.5 km long link is meteorologically marginal and the v1/v2 runs were **not**
> interleaved; v1's remote cluster failed to join in 3 of 4 cells (shown by the
> participation panel), so only SF9_d63000 is a directly comparable cell. On
> `sim_size_compare`, the 120/180 B cells deliver 0 on **both** stacks (payload > single-
> packet MTU, no app fragmentation) and SF9_s40 v2 is a harness capture-window artifact —
> only SF9_s80 and SF12_s40 are valid. A clean interleaved re-run with `linkstats` capture
> is the fix.
