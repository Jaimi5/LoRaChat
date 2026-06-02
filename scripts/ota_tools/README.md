# OTA Tools

Host-side Python tooling for over-the-air firmware updates to LoRaChat mesh nodes.

## Scripts

| Script | Purpose |
|---|---|
| `generate_patch.py` | Generate a delta patch and optionally split into OTA chunks in one step |
| `split_chunks.py` | Split any binary into numbered chunks for MQTT or LoRa transport |
| `ota_campaign.py` | Deploy firmware OTA to a target node (main orchestrator) |
| `ota_monitor.py` | Monitor OTA progress from all nodes via MQTT |
| `build_and_deploy.py` | PlatformIO post-build hook for automatic patch generation |

## Installation

```bash
pip install -r requirements.txt
```

## Usage

### Generate a delta patch

```bash
python generate_patch.py --old v1.bin --new v2.bin --out patch.bin --version "2.0.0"
```

Always outputs **two files**:

| File | Contents |
|---|---|
| `patch.bin` | Binary delta patch to transfer to the node |
| `patch.json` | Metadata: `old_size`, `new_size`, `patch_size`, `new_crc32`, `fw_version` |

`ota_campaign.py --delta` requires **both files** in the same directory.
It reads `patch.bin` to stream chunks and `patch.json` to get the new firmware's
size and CRC32 — the values the node verifies after applying the patch.

### Generate a patch and split in one step (optional)

The `--split` flag additionally chunks the patch into numbered files for inspection.
The patch binary is always kept — `ota_campaign.py` reads it directly.

```bash
# For LoRa nodes (222-byte chunks)
python generate_patch.py --old v1.bin --new v2.bin --version "2.0.0" \
    --split --transport lora --output-dir lora_chunks/

# For MQTT nodes (400-byte chunks)
python generate_patch.py --old v1.bin --new v2.bin --version "2.0.0" \
    --split --transport mqtt --output-dir mqtt_chunks/
```

`--split` is for offline inspection only — it is **not** required before running a campaign.

### Deploy a delta patch (end-to-end)

`ota_campaign.py` handles chunk splitting internally.

```bash
# Step 1 — generate the patch (creates patch.bin + patch.json)
python generate_patch.py \
    --old .pio/build/ttgo-t-beam-v2/firmware_v1.bin \
    --new .pio/build/ttgo-t-beam-v2/firmware.bin \
    --out patch.bin \
    --version "2.0.0"

# Step 2 — run the campaign (ACK/retry handled automatically)
# --target accepts decimal (15071) or hex (0x3ADF) — both work
python ota_campaign.py \
    --target 0x3ADF \
    --firmware patch.bin \
    --delta \
    --version "2.0.0" \
    --host 192.168.1.26

# NOTE: the MQTT topic uses the decimal value regardless of how you pass --target
# i.e. the node publishes to "to-server/15071", not "to-server/0x3ADF"
```

For LoRa transport add `--transport lora` to `ota_campaign.py` (222-byte chunks instead of 400).

The campaign sends `OTA_BEGIN` → streams all chunks with ACK-based retransmit
(timeout 10 s, max 5 retries) → sends `OTA_APPLY`. The node applies the patch,
verifies the result against `new_crc32`, then reboots automatically on success.

### Deploy full firmware over MQTT

```bash
python ota_campaign.py \
    --target 15071 \
    --firmware .pio/build/ttgo-t-beam-v2/firmware.bin \
    --host 192.168.1.26
```

### Inspect pre-split chunks (optional)

```bash
python generate_patch.py --old v1.bin --new v2.bin --version "2.0.0" \
    --split --transport lora --output-dir lora_chunks/
# produces lora_chunks/chunk_000000.bin … chunk_000230.bin
```

### Monitor OTA progress

```bash
python ota_monitor.py --host 192.168.1.26
```

Subscribes to `to-server/+` and displays per-node progress bars for `ACK` and `STATUS` messages.

### Split chunks manually (any binary)

`split_chunks.py` works on any binary — full firmware or patch file:

```bash
python split_chunks.py --input firmware.bin --transport lora --output-dir chunks/
python split_chunks.py --input patch.bin    --transport mqtt --output-dir chunks/
```

## Chunk sizes

| Transport | Chunk size | 50 KB patch |
|---|---|---|
| MQTT | 400 bytes | 128 chunks (~seconds) |
| LoRa | 222 bytes | 231 chunks (~40–77 min at 1% duty cycle) |

## MQTT defaults

Matching `src/config.h`: broker `192.168.1.26:1883`, username `admin`, password `public`.
Topics: `from-server/{node}` (host→node), `to-server/{node}` (node→host).

`{node}` is always a **decimal integer** (e.g. `15071`), matching Arduino's `String(uint16_t)`
which formats decimal by default. The `--target` option accepts both `15071` and `0x3ADF`.

