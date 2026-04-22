# LoRaChat — TODO

Follow-ups deferred from the `LoRa_Receive` stack-overflow fix (see plan at
`/home/jan/.claude/plans/can-you-analyze-and-precious-volcano.md`). The
immediate overflow was resolved in Phase 1 by increasing stack sizes and
moving the heavy `StaticJsonDocument`s to heap; the items below address
the architectural anti-patterns that made the overflow possible.

---

## 1. Move `writeToMqtt` off `LoRa_Receive` via outbound queue

**Why:** `MqttService::processReceivedMessage` (`src/mqtt/mqttService.cpp:161-164`)
currently calls `writeToMqtt` synchronously on the `LoRa_Receive` task,
which then runs `getJSON` + `esp_mqtt_client_publish` (lwIP TCP) on the
receive path. Phase 1 bumped the stack to 8 KB, which removes the immediate
overflow, but the coupling remains — any future growth in `getJSON` or
lwIP stack use can reintroduce the bug.

**How:**
- Add an outbound `sendQueue` in `MqttService` mirroring the existing
  inbound `receiveQueue` at `src/mqtt/mqttService.h:96`.
- `MqttService::processReceivedMessage` should heap-copy the `DataMessage`
  and enqueue a pointer; return immediately.
- The existing `Mqtt Task` (`src/mqtt/mqttService.cpp:30`, currently only
  drains inbound) takes on outbound: dequeue, `getJSON`, publish, free.
- Bump `Mqtt Task` stack from 4096 to 6144 B when adding this — it
  inherits the heavy `getJSON` path that currently lives on `LoRa_Receive`.
- Watch ownership of `DataMessage*` across the task boundary carefully;
  `MessageManager::processReceivedMessage` frees the message after
  dispatch (`loraMeshService.cpp:112` does `vPortFree(qMsg->dataMessage)`
  right after the dispatch returns), so the queue needs to own a copy.

## 2. Enable core dump to flash

**Why:** The recent 87 panics were diagnosed by inference from log lines.
Post-mortem core dumps would give exact stack contents and watermarks.

**How:**
- Add `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` (and
  `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y`) to the relevant
  `sdkconfig.*` files.
- Reserve a `coredump` partition in `partitions.csv`.
- Document how to read dumps with `espcoredump.py info_corefile` in
  `docs/` (new file).

## 3. Extend `uxTaskGetStackHighWaterMark` coverage

**Why:** Phase 1 added the watermark to `loraReceiveLoop`. The following
tasks still lack it per the task inventory done during diagnosis:
- `Receive App Task` (v1 receive path, `loraMeshService.cpp:374`, 5000 B)
- `OTAWatchdog` (`otaService.cpp:44`, 2048 B)
- `OTAPatch` (`otaService.cpp:230`, 8192 B — created dynamically)
- `SimTask` (`sim.cpp:83`, 8192 B)

**How:** Add `ESP_LOGD(TAG, "stack high water: %u", uxTaskGetStackHighWaterMark(NULL));`
at a natural point in each task loop. Low priority — these tasks haven't
crashed, this is preventive.

## 4. Investigate UNIDIRECTIONAL link spam on 1484 and E464

**Why:** Flagged by the LoRaMesher team in the stack-overflow handoff —
not related to the overflow, but noticed during the same audit. Node 1484
logs 1659 `[UNIDIRECTIONAL]` events in 15 h; E464 logs 1167. 1484 does not
crash despite the high rate, so it's a link-quality issue, not a mesh-code
bug.

**How:** Physical-placement / antenna audit of 1484 and E464. Compare
RSSI/SNR distributions against the 11 clean nodes. Mesh-side: check
whether the unidirectional detection threshold is too aggressive for the
current SF9/Power20 regime.
