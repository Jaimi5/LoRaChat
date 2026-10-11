# LoRaMesher 2.0.0 (764b893) task stack headroom — all testbed runs

Collected 2026-10-10 with `scripts/testbed/analysis/grab_stack_warnings.py` from every valid
2.0.0 run of the lm200 campaign: smoke, formation, 13node, reach16, dataslots, A/B, features,
and the features re-run. Each sub-folder holds that run's `warnings.log`, `stack.csv` and `summary.md`.
Units are bytes, as reported by 2.0.0. Do not compare with 134ae25 logs, which reported free stack in words.

| Task | Configured (B) | Worst min free (B) | Worst run / node | 5th pct of per-node min | Median per-node min | Samples (node×run) |
|---|---|---|---|---|---|---|
| LoRaMeshMain | 4096 | **448** | lm200_features nm_failover_SF9 / 77A4 | 496 | 580 | 655 |
| RadioEvent | 3280 | **484** | lm200ab_13node SF9 r03 / C270 | 520 | 788 | 655 |
| SuperframeUpdate | 3072 | 1072 | lm200_features slot_overflow_SF9_max8 / 2A9C | 1168 | 1344 | 655 |

`[WARNING] Task … stack low` lines: LoRaMeshMain 62,928 and RadioEvent 66,876. SuperframeUpdate never warns.
The warnings fire on nearly every node in every run, so they show steady-state usage, not rare peaks.

**Suggested sizing.** Target at least 1 KB of headroom at the observed worst case:
- **LoRaMeshMain:** 4096 → 5120 B gives about 1.5 KB free.
- **RadioEvent:** 3280 → 4096 B gives about 1.3 KB free.
- **SuperframeUpdate:** 3072 B is fine. About 1 KB free in the worst run.

**Parser note.** Earlier gate output showed SuperframeUpdate at 1 B and 13 B free. Those were serial lines cut off by another task's output, for example `free=1I (182274) …` where the real value was `free=1216`. The parsers now reject cut-off lines.
