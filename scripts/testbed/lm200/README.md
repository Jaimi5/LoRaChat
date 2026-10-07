# LoRaMesher 2.0.0 unattended testbed campaign

One command validates LoRaMesher `release/2.0.0` (764b893) on the 16-node testbed
against the July 134ae25 results. It writes a GO / GO-WITH-CAVEATS / NO-GO report;
nobody has to watch it.

## Before launching (once)

1. All code is committed **and pushed** on the campaign branch. The campaign refuses
   to start if local `HEAD` differs from `origin/<GIT_BRANCH>`, and it never pushes:
   ```
   git push origin lm200-eval
   ```
2. `scripts/testbed/testbed.conf` has `GIT_BRANCH="lm200-eval"` and working gateway SSH.
3. Optional: set `notify:` in `lm200/campaign.yaml`, for example
   `'curl -s -d {summary} ntfy.sh/<topic>'`.
4. Optional: rehearse the run without touching the testbed. A dry run renders every
   batch into throw-away `runs/lm200dry_*` dirs and runs the gates on existing runs:
   ```
   python3 scripts/testbed/lm200/campaign.py --dry-run --expect-sha 134ae25 --expect-radiolib '' \
     --fixture-runs g0=runs/sim_load_compare_13node__lmv2/sim_load_compare_13node-20260724-160746-SF9_d63000-r01 \
                    13node=runs/sim_load_compare_13node__lmv2
   ```
   July runs predate the RadioLib pin (their upload logs have no `RadioLib @` line), so the
   RadioLib check is disabled for this rehearsal; G0 on that July SF9 run is GREY only for the
   2.0.0-only even-join-slot check.

## Launch

```
cd /mnt/d/Projects/ESP32/lorachat/LoRaChat
nohup python3 scripts/testbed/lm200/campaign.py > lm200_campaign.out 2>&1 &
```

## What happens

| Stage | What | Gate | ~Wall-clock |
|---|---|---|---|
| preflight | Checks HEAD == origin, runs `deploy.sh status` (retries up to 30 min), `deploy.sh upgrade`, and verifies every gateway is at HEAD | – | 5 min |
| prewarm | `pio run -e ttgo-t-beam-v2` on every gateway, retried, wiping `.pio/libdeps/<env>/LoRaMesher` if it keeps failing. RadioLib per gateway is logged, never changed | – | 10–35 min |
| T0 | `lm200_smoke`: SF9, 13-node cluster, 20 min | **G0**: flash, lib sha, RadioLib consistency, 16/16 boot, no crash/reboot, no LDRO/ToA mismatch, build env, all joined, single NM, routing tables complete, fair PDR ≥ 0.95, superframe vs July, even join slots. **FAIL stops the campaign** | ~45 min |
| T1a | `lm200_formation` ×3, `lm200_13node`, `lm200_reach16`, `lm200_dataslots` | **G1** vs July, values computed from `runs/*__lmv2` with the same code: fair PDR per cell, convergence ≤ July + 1 superframe, join completeness, join retries, dual NM, data slots granted | ~11–12 h |
| T1b | Only if G1 has GREY regression checks: interleaved A/B, `ttgo-t-beam-v2-old` (134ae25) vs `ttgo-t-beam-v2`, 3 rounds of `lm200ab_13node` | – (figures + A/B dirs) | ~16 h |
| T2 | `lm200_features*.yaml`: reliable SF7/SF9, NM failover, slot overflow, group, Stop/Start, SF11 cold join, long link SF10/20 dBm | **G2**: ACK ratio, RTT, group delivery and duplicates, rejoin, sequence continuity, NM takeover, zero-slot nodes, remote join, stack and heap headroom, hangs | ~10.5 h |
| report | `runs/lm200_campaign/report/{report.md,report.html,verdict.json}` plus figures | verdict | min |

The **verdict** works like this:
- **NO-GO:** any blocker. Examples are a crash, an unexpected reboot, a dual NM, a wrong lib sha, a PDR or convergence regression beyond the July spread, a failed rejoin, or a failed takeover.
- **GO-WITH-CAVEATS:** GREY or INCOMPLETE checks, INCOMPLETE cells, or RadioLib differing between gateways.
- **GO:** everything else.

## Self-healing

- Every batch invocation runs `deploy.sh upgrade` first. Its `git reset --hard` restores
  the committed `src/config.h`, so no value sed-edited by a previous batch leaks into the next one.
- After each batch, every run is validated:
  - the critical lifecycle phases succeeded;
  - every board flashed, if the run uploaded;
  - the expected LoRaMesher sha was built;
  - every board has a log with a boot banner.

  An invalid run is moved to `<run>.FAILED-<ts>` (never deleted). The cell is then re-run as a
  single-cell temp batch with the same batch name, up to `retries.cell_attempts`, with a
  `deploy.sh sync-time` in between. After that the cell is recorded as INCOMPLETE and the
  campaign moves on.
- If a gateway can't prewarm, the campaign stops with NO-GO, because every run needs all gateways.

## Where results land

- `runs/lm200_{smoke,formation,13node,reach16,dataslots,features}/`: run dirs, in the same format as all other batches.
- `runs/lm200ab_13node__{lm134,lm200}/`: the T1b A/B dirs (only if triggered).
- `runs/lm200_campaign/`: `state.json`, `campaign.log`, `logs/` (output of every command), `gates/*.json`, `cache/` (run summaries; baseline dirs are never written to), `report/`.

## Resume, re-run, abort

- **Resume** after a crash or reboot of the host: run the same launch command again. Finished stages and batches are skipped.
- **Re-run from a stage:** `campaign.py --from-stage T1a`, or `--only-stage T2 report`.
- **Start over:** `campaign.py --reset-state`. This archives `state.json`; run dirs are kept.
- **Rebuild only the report:** `python3 scripts/testbed/lm200/report.py`.
- **Abort:**
  ```
  pkill -f lm200/campaign.py; pkill -f runner/run_batch.py
  bash scripts/testbed/deploy.sh stop-monitor
  ```

## Gates by hand

```
python3 scripts/testbed/lm200/gates.py g0 runs/lm200_smoke
python3 scripts/testbed/lm200/gates.py g1 formation=runs/lm200_formation 13node=runs/lm200_13node \
        reach16=runs/lm200_reach16 dataslots=runs/lm200_dataslots
python3 scripts/testbed/lm200/gates.py g2 runs/lm200_features
python3 scripts/testbed/lm200/gates.py g_minus1 build-ttgo-t-beam-v2.log   # local pio build output
```

`--expect-sha 134ae25` points the sha checks at the old library, for example to self-test on July data.

## Notes

- **Per-device flags** live in the base experiments under `lm200/experiments/`: the group sender on
  7B6C, Stop/Start on CB34/2A9C/B4DC, and 20 dBm on 006C/3428. Batch cells can only override
  `defaults`. Every flag is also set in `defaults`, because the per-gateway sed edits one shared
  `config.h`.
- **The smoke run keeps the remote cluster inactive.** At SF9 the long link flaps for link-margin
  reasons, and a GO/NO-GO on the library shouldn't depend on that. The long link is covered by
  `lm200_reach16` and the `long_link_SF10` feature cell.
- **T1b needs `ttgo-t-beam-v2-old` to compile** with the current LoRaChat sources. If it doesn't,
  prewarm of the old env fails and T1b is recorded as failed.
