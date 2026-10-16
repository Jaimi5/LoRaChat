# Plan: 7-day SF10 soak of the default LoRaChat firmware on LoRaMesher 2.0.0

## Resume here in a new session (status 2026-10-16)
- **Status: PLAN ONLY, nothing executed yet.** No code changed, nothing pushed, the testbed untouched since the #136 round.
- **User decisions (2026-10-16):**
  - Default firmware, as in the earlier full_deploy runs, on the new 2.0.0 library at SF10.
  - **7 days**.
  - Evidence is a **daily-updated private report page**, shared by the user.
  - **The PC must not need to stay on**: no keep-awake, and everything continuous runs on the gateways.
- **Earlier context:**
  - The PR #136 hardware round ended GO. Report: `LoRaChat-sleep/scripts/testbed/lm200/FIX136_REPORT.md` (lm200-sleep 2420a2c). Page: https://claude.ai/artifact/AKr5T84Qq9uEaXe9ZKvNwv
  - #136 is now merged; release/2.0.0 head is 41f9b68.
- **Standing rules:**
  - Never change library code, and never touch `/mnt/d/Projects/ESP32/loramesher` (the user's uncommitted work); read-only git there is fine.
  - Never commit `testbed.conf` or `experiments/*.yaml` (credentials).
  - No private IPs in public issues or PRs.
  - The classifier denies me killing gateway or campaign processes, so I ask the user to run `! <cmd>` on one line.
  - Track PIDs, never use `pkill -f` myself, and never edit running scripts in place.
  - The user has authorised pushing lm200-sleep and the other LoRaChat branches.
- **Key facts found:**
  - The previous default run, `full_deploy_sf10_v3`, used branch new_loramesher (34951ba) and env ttgo-t-beam-v2. It ran 28 Sep–5 Oct with 12 nodes and produced 3.9 GB of logs; they are in `../LoRaChat/logs_testbed/`.
  - `origin/new_loramesher` is an ancestor of lm200-sleep, 39 commits behind, so a fast-forward is possible.
  - `current.yaml` (SF10, manager 006C, 006C and 3428 at 20 dBm, mon 900 s) exists only in `../LoRaChat/scripts/testbed/experiments/`.
  - In LoRaChat-sleep, `testbed.conf` has `GIT_BRANCH="lm200-sleep"`.
  - Monitors have no restart or rotation. `deploy.sh clean` has a find-precedence bug, so don't use it.

## Context
The campaigns are over and #136 is merged. release/2.0.0 head is now **41f9b68**, which includes #136, member-sleep #134, task-gate #138 and warm-restart #133.

The user wants the testbed back on the **default deployment as it was before**:
- the normal LoRaChat firmware, not the simulator;
- monitor messages over MQTT;
- `experiments/current.yaml`: SF10, 006C manager, 006C and 3428 at 20 dBm, everything else at 2 dBm, `mon_sending_every` 900 s.

This time it runs the new library, for **7 days**. A **daily-updated report page** gives people confidence that the protocol works.

The baseline is the previous default run, `full_deploy_sf10_v3`: same config, old library at 134ae25, 7 days (28 Sep–5 Oct), on branch `new_loramesher`, env `ttgo-t-beam-v2`. Its logs are in `../LoRaChat/logs_testbed/`. `full_deploy_sf10_v4` was never collected, which is why it is only 152 KB.

## 1. Firmware and branch (LoRaChat-sleep, then fast-forward new_loramesher)
Work on `lm200-sleep`. `origin/new_loramesher` (34951ba) is a strict ancestor of it, 0 commits ahead and 39 behind.
- **`platformio.ini` `[base_v2]`:** pin LoRaMesher to **41f9b68**. Pin the exact SHA rather than the branch name, so the whole week runs one fixed version. Keep RadioLib 7.7.1. Leave `-old` (134ae25) and `-sleep` as they are.
- **`src/monitor/monService.cpp:131-132`:** the route count does not check `is_valid`, but the fill loop at `:155` does. When a direct route goes stale, the last monitor message is never filled or sent, and its buffer leaks every cycle. That is fatal over 7 days. Fix: count with `route.is_valid && route.destination == route.next_hop`, which is the commented line already there. This is app code, not library code.
- **Log volume:** the library logs at DEBUG by default (`system_config.hpp:18`), about 55 MB per node per day.
  - Set `-D LORAMESHER_LOG_LEVEL=1` (INFO) on `ttgo-t-beam-v2`, but only after checking that the define is `#ifndef`-guarded in 41f9b68.
  - Every line the report needs is still printed at INFO: `PKT_RX/TX`, join, FaultRecovery, `STACK`, `HEALTH`, `BOOT`.
- **`scripts/testbed/gw-monitor.sh` start:** wrap the `script … pio device monitor` pipeline in `while true; do …; sleep 5; done`. A USB drop then restarts the monitor instead of killing it silently, which is what happened to the remote monitors in full_deploy4. `stop` kills the process group, so it still works.
- **Build check:** run `scripts/pio.sh run -e ttgo-t-beam-v2` with T_BEAM_V10, and check that RAM and flash look normal.
- **Commit and push:**
  - Commit and push `lm200-sleep`.
  - Fast-forward the default branch: `git push origin lm200-sleep:new_loramesher`. This is the "merge into new_loramesher after the verdict" step recorded in memory.
  - Write the memory notes (step 4) before pushing.

## 2. Deploy
From `LoRaChat-sleep/scripts/testbed`. The gitignored files are never committed.
- **`testbed.conf`:** set `GIT_BRANCH="new_loramesher"`.
- **`current.yaml`:** copy `../LoRaChat/scripts/testbed/experiments/current.yaml` to `experiments/current.yaml`, then `configtool validate`. Only 77A4 has real WiFi credentials, so it is the MQTT uplink, as before.
- **Preflight:**
  - `deploy.sh status`.
  - `df -h ~` on every gateway via `run-remote`. Expect about 0.4 GB per node per week at DEBUG, a fraction of that at INFO.
  - Gateways still hold about 167 April–July run dirs. Do **not** run `deploy.sh clean`, which has a known find-precedence bug. If a gateway is short of space, stop and ask.
- **Launch:** `SSH_AUTH_SOCK=~/.ssh/lm200-agent.sock ./deploy.sh all -n soak_200_sf10`. This does stop-monitor, then upgrade (with the `pio pkg update` retry), then push-config (which resets config.h per device), then upload with monitors.
- **If a gateway hangs:** as with GW-4 before, ask the user to run the kill. The classifier denies me killing gateway processes.
- **Smoke check, about 30 min after upload:**
  - all 16 nodes logging;
  - `LoRaMesher @ 2.0.0+sha.41f9b68` in every upload log;
  - `CFG` / config lines show SF10;
  - 3428 joined via 006C;
  - no reboot loops.
## 2b. PC-independent operation
The PC and this session do **not** need to stay on. Nothing in the run depends on the PC, and no keep-awake process is used. The PC is only needed for a few minutes whenever the report is updated. Gateway-side pieces, all in `gw-monitor.sh` and pushed through git with the deploy:
- **Restart a monitor after a USB drop:** the `while true` loop from step 1.
- **Restart monitors after a gateway reboot or power cut:**
  - `start` writes its arguments (session, env, device:port list) to `logs/.current_monitor`.
  - A new `gw-monitor.sh resume` action reads that file and restarts the monitors.
  - deploy installs an idempotent user crontab line on each gateway: `@reboot sleep 60 && bash $REPO/scripts/testbed/gw-monitor.sh resume`.
  - `stop` deletes `.current_monitor`, so a stopped run never comes back by itself.
- **Daily rotation on the gateway, cron at 00:05:**
  - For each `monitor-dev-*.log`, copy it to `…-YYYYMMDD.log`, `truncate -s 0` the live file, and gzip the copy.
  - Truncating is safe because the monitor appends with `>>` (O_APPEND).
  - This bounds the live file and turns each finished day into a small immutable `.gz` file.
- **Check gateway clocks** with `deploy.sh sync-time` before the launch. The day boundaries depend on them.

On the PC, a new `scripts/testbed/soak_update.sh`:
- copies only the day files it doesn't have yet (via `run-remote`/scp of `*-YYYYMMDD.log.gz`; rsync if a gateway has it);
- then runs `soak_report.py` and renders the HTML.

If the PC is off for several days, the next update catches up all missing days.

## 3. Daily report
- **New `scripts/testbed/analysis/soak_report.py`:** reuses `analysis/parse_logs.py` (gz and plain readers, `v2_runtime_addr`) and the read loop from `routing_slice_checks.py`.
- **Per node and per day it reports:**
  - **Uptime and reboots:** `BOOT reason=`, and `rst:` other than power-on.
  - **Monitor gaps:** log silence over 10 min, which means the monitor or USB died rather than the node.
  - **Joins and fault recoveries:** `Joining network`, `Join response`, `entering FaultRecovery`, `Join timeout`. These are counted as flaps, which `formation.py` cannot see.
  - **Time joined:** % of time in the joined state, from `HEALTH … state`.
  - **Sync-beacon reception rate:** `PKT_RX` of beacons from the manager against the expected superframe count.
  - **Long link (006C↔3428):** RSSI/SNR median and p10 per day, plus the beacon and routing-table reception rate.
  - **Monitor messages:** each node's monitor messages TX'd, and those seen RX'd or published at the uplink.
  - **Health:** `HEALTH` heap and min-heap trend (a leak check), `STACK` minimum free per task, and new WARN/ERROR lines.
- **Fleet summary:** a green/amber/red per check, plus the same metrics computed over `full_deploy_sf10_v3` as the old-library comparison.
- **Update cycle, whenever the PC is on:**
  - The user asks "update the soak report", or I do it at the start of any session.
  - `soak_update.sh` fetches the finished days and runs `soak_report.py --json`. Today's partial day is optional and uses a live snapshot.
  - Render the HTML and republish to the **same artifact URL**, created as private on day 1. The user shares the link.
  - Each day the page shows "data up to <date>", so a missed update is visible and nothing is lost.
- **Day 7:** stop the monitors with `deploy.sh stop-monitor`, which gzips the logs, then do a final `logs` collect and the final report.
  - The final report goes in `scripts/testbed/lm200/SOAK_200_SF10_REPORT.md`, plus the final page.
  - Commit `soak_report.py` and the report, and push.

## 4. Memory
- Update `project_lm200_campaign.md`: GIT_BRANCH restored to new_loramesher, and the soak is running with its session name and artifact URL.
- Note the monService leak fix and the monitor auto-restart in the relevant memory.

## Verification
- **Build:** `ttgo-t-beam-v2` builds. The upload logs show sha 41f9b68 on all 7 gateways.
- **Smoke at 30 min:** all 16 nodes logging, all joined, manager 006C, SF10, the 3428 long link up.
- **Analysis:** `soak_report.py` runs first on `full_deploy_sf10_v3`, giving the baseline numbers, and on the smoke snapshot. I spot-check its join and FaultRecovery counts against a manual grep on one node.
- **Each update:** the page republishes to the same URL, and its numbers match a grep spot check.
- **PC independence, tested at the smoke stage:**
  - On one gateway, kill one monitor's `pio` process and confirm it restarts within about 10 s.
  - Run `gw-monitor.sh resume` by hand and confirm it restores the session.
  - Check the crontab lines on all 7 gateways (`crontab -l` via `run-remote`).
  - Run the rotation once by hand on a test copy, and confirm the live file keeps growing after the truncate.
- **Go/no-go criteria for the "works" claim:**
  - **Main cluster:** 0 unexpected reboots, 0 fault recoveries, ≥99 % of time joined, heap flat.
  - **Long link:** compared against v3 and reported, not gated, because it is weather and link dependent.
