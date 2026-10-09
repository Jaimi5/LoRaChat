# OTA tools

Host tools for versioning, releasing, signing and testing firmware images. Run them from the project root under WSL or Linux.

```bash
pip install -r scripts/ota_tools/requirements.txt   # cryptography (signing) and pytest
python3 -m pytest scripts/ota_tools/tests test/host # tests of these tools
```

| Tool | Purpose |
|---|---|
| `version_hook.py` | Build hook that sets the image version from `version.txt` |
| `release.py` | Checks a build and collects it into `release/<env>/<version>/`, optionally signed |
| `sign_manifest.py` | Keys, signed manifests, manifest inspection and test vectors |
| `build_test_images.sh` | Builds the boot-guard fault images, signed with the test key |
| `check_guards.py` | Static checks that keep app rollback working |
| `otadata.py` | Reads and rewrites the `otadata` partition to boot a chosen slot (bench) |
| `fw_server.py` | Bench firmware server for the WiFi pull, with injected faults |
| `ap_pass.py` | Deployment key, and the name, password and QR text of a node's access point |
| `flash_floor.py` | Flashes the floor image on a node over USB and provisions it (key, WiFi, role, inventory) |

| `lmcmd.py` | Signs commands for nodes, and encrypts WiFi credentials for `/maint.wifi` |
| `ap_upload.py` | Bench: uploads a bundle to a node's access point through a second T-Beam (`extras/bench_ap_bridge`) |

## Versions

`version.txt` holds the version as `MAJOR.MINOR.PATCH`, each part 0-255. `version_hook.py` runs before every `tbeam` build and writes it into the image (`esp_app_desc_t.version`) with git metadata:

- `0.1.0+gabc1234`: built from commit `abc1234`
- `0.1.0+gabc1234.dirty`: tracked files had uncommitted changes

The node prints it on its `BOOT ...` line. `FW_VERSION=0.1.1` in the environment overrides `version.txt` for one build (used for test images). The manifest carries the version packed as `0xMMmmpp00`; build metadata after `+` is ignored when versions are compared.

## Keys

Manifests are signed with ECDSA P-256. The node only installs images whose manifest is signed by a key it trusts (`src/ota/otaKeys.cpp`).

| Key id | Key | Use |
|---|---|---|
| `1` | `test/vectors/test_key.pem` (in the repository) | Bench and CI images. Refused for installs by production builds (`OTA_PRODUCTION`) |
| `0x100` and up | Production keys, never in the repository | Field images |

Create a production key once and keep the private key offline (password manager, encrypted USB stick):

```bash
python3 scripts/ota_tools/sign_manifest.py keygen --out /secure/place/lm_prod_0100.pem
```

It never overwrites an existing file and writes it with mode 0600. The command prints the public key as a C array: add it to `otaTrustedKeys()` in `src/ota/otaKeys.cpp` with its id (`makeKey(0x100, ..., false)`) and release a firmware that contains it before you sign anything with it.

## Making a release

1. Set `version.txt`, commit, and build: `scripts/pio.sh run -e tbeam`
2. Collect and sign:

   ```bash
   python3 scripts/ota_tools/release.py --env tbeam \
       --sign-key /secure/place/lm_prod_0100.pem --key-id 0x100
   ```

   This writes `release/tbeam/<version>/` (gitignored):

   | File | Content |
   |---|---|
   | `firmware.bin` | App image, written to `app0`/`app1` |
   | `bootloader.bin`, `partitions.bin`, `ota_data_initial.bin` | For a full USB flash |
   | `firmware.elf`, `firmware.map` | For backtraces and size analysis; keep them with the release |
   | `info.json` | Version, `version_u32`, image size and slot use, `image_sha256`, ELF, bootloader and partition table hashes |
   | `manifest.bin` | Signed manifest, 192 bytes (with `--sign-key`) |
   | `bundle.bin` | `manifest.bin` followed by `firmware.bin`, for the upload through the node's AP |

   Before copying, `release.py` checks the SHA-256 that ESP-IDF appends to the image, the 90 % size gate of the app slot (`partitions.csv`), and that the image version matches `version.txt`. It refuses `.dirty` builds unless `--allow-dirty` is given (bench only).
3. Optional flags of the manifest: `--allow-downgrade` lets nodes install it over a newer version (the way back from a bad image that passed the self-test); `--skip-mesh-check` skips the boot self-test's mesh-contact check (for a release that changes the mesh protocol).

To sign an already collected release, or to sign it again with other flags:

```bash
python3 scripts/ota_tools/sign_manifest.py sign release/tbeam/0.1.0+gabc1234 \
    --key /secure/place/lm_prod_0100.pem --key-id 0x100 [--allow-downgrade] [--skip-mesh-check]
```

To inspect a manifest and check its signature (`--key` takes the private key or a public PEM key):

```bash
python3 scripts/ota_tools/sign_manifest.py show release/tbeam/0.1.0+gabc1234/manifest.bin \
    --key test/vectors/test_key.pem
```

The image identity used by the manifest, the blacklist and the reports is `image_sha256`: the SHA-256 that ESP-IDF appends to `firmware.bin`, equal to `esp_partition_get_sha256()` on the node.

## Manifest format

128-byte body followed by a 64-byte ECDSA P-256 signature (raw `r || s`) over the SHA-256 of the body. Little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic `LMOT` |
| 4 | 1 | Format version (1) |
| 5 | 1 | Image type (1 = full app image) |
| 6 | 2 | Flags: bit 0 allow downgrade, bit 1 skip mesh check |
| 8 | 4 | Key id |
| 12 | 4 | Version `0xMMmmpp00` |
| 16 | 4 | Image size |
| 20 | 4 | Reserved (0) |
| 24 | 16 | Board env (`tbeam`), zero-terminated |
| 40 | 16 | Project (`LoRaChat`), zero-terminated |
| 56 | 32 | Version string, zero-terminated |
| 88 | 32 | Image SHA-256 |
| 120 | 8 | First 8 bytes of the SHA-256 of `partitions.bin` |

The firmware side is `lib/OtaCore/src/otaManifest.cpp`; `sign_manifest.py vectors` regenerates the shared test vectors (`test/vectors/`) and the firmware's copy of the test key (`src/ota/otaTestKey.h`) after a change of the format or of the test key.

## Bench procedures

The boards are flashed from Windows-native Python because WSL loses USB devices on every power cut. PlatformIO's Windows Python has `pyserial`; esptool is `%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py`. Copy the release folder to a Windows path first (`release/` in the project is on `D:`).

Full USB flash of a release (bootloader, partition table, `otadata` reset to `app0`, app):

```bat
python esptool.py --chip esp32 --port COM6 --baud 921600 --after no_reset write_flash ^
    0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 ota_data_initial.bin 0x10000 firmware.bin
```

Open the serial port with DTR and RTS low before `open()` and reset once with RTS. Any second reset while a new image is in `PENDING_VERIFY` counts as a failed boot and rolls back.

Boot-guard test without OTA: put an image in the other slot and make the bootloader try it as a new image.

```bash
# 1. write the image to app1 (0x1D0000) with esptool, and read otadata:
#    esptool.py ... write_flash 0x1D0000 firmware.bin
#    esptool.py ... read_flash 0xe000 0x2000 otadata.bin
# 2. select slot 1 in state NEW
python3 scripts/ota_tools/otadata.py decode otadata.bin
python3 scripts/ota_tools/otadata.py select --slot 1 --state new -o otadata_new.bin otadata.bin
# 3. esptool.py ... --after no_reset write_flash 0xe000 otadata_new.bin, then one reset
```

Expected serial output: `BOOT part=app1 state=PENDING_VERIFY ...`, `OTA path: signature check ok`, then `Self-test passed, image marked valid` and `BOOT part=app1 state=VALID`. With a fault image the node rolls back: it boots `app0` again and prints a `BOOT last_invalid=app1` line.

Update over WiFi (pull) on the bench. The node's WiFi network must reach the PC, and `OTA_SERVER_URL` in `config_local.h` must point at it, e.g. `"http://192.168.1.50:8070/"`. Run the server with Windows Python, since WSL is not reachable from the LAN, and allow it through the Windows firewall once:

```bat
python fw_server.py D:\path\to\release\tbeam\0.1.1+gabc1234
```

A sensor node checks the server in its boot window, or in a window opened with `/maint.open 60`. Expected serial output: `Manifest for tbeam 0.1.1+gabc1234, ...`, `Installing ...`, `Image OK after 1 attempt(s): ... B in ... ms (flash ... ms)`, `Rebooting into ... on app1`, then the boot guard lines above. The server logs every request. With `--report-key-file deploy.key` the server also takes the nodes' signed `POST /report` and prints each report (`report 7680 written {...}`), as the web server does. Faults for the bench cases: `--flip-byte N` (one byte changed: `SHA_MISMATCH`, nothing installed), `--cut-at 0.5 [--cut-times K]` (connection closed at 50 %: up to 3 attempts, then `TRANSPORT_FAILED`), `--missing-image` (404). After a failure the node restarts its current image and skips that image until the next power-on.

## Deployment key and the node's access point

All nodes of a deployment share one 32-byte key. It derives the password of each node's maintenance access point and will also sign commands and reports (F5, F7). Create it once and keep it offline:

```bash
python3 scripts/ota_tools/ap_pass.py keygen --out /secure/place/deploy.key
```

Load it on each node over USB serial with `/key.set <the 64 hex digits in the file>`; `/key` prints its fingerprint, which `ap_pass.py show` prints too. For a node with mesh address `7680`:

```bash
python3 scripts/ota_tools/ap_pass.py show --key-file /secure/place/deploy.key --node 7680
```

prints the access point name (`LM-7680`), its WPA2 password (`hex(HMAC-SHA256(K, "LMAP1" || address)[:8])`) and the WiFi QR text for a sticker. `ap_pass.py vectors` regenerates `test/vectors/apPassVectors.h`, which the native tests compare with the firmware.

Upload through the access point: `/maint.open 600 ap` on the node, join `LM-<address>` from the phone (QR) or a PC, open `http://192.168.4.1/` and upload `bundle.bin` of a signed release. From a PC: `curl --data-binary @bundle.bin http://192.168.4.1/update`. The reply is `OK: <version> written ...` (the node reboots into it and the boot guard decides) or `ERROR <status>`, e.g. `ERROR MANIFEST_REJECTED: REFUSE_DOWNGRADE`; a refused manifest leaves the flash untouched and the access point open. Once the mesh is stopped for writing, a failure restarts the node on its current image (the access point closes).

Bench without a phone: a second T-Beam runs `extras/bench_ap_bridge` and acts as the client, so the PC stays on its own network. Build it with `PLATFORMIO_WORKSPACE_DIR=~/.cache/pio-ws/bench_ap_bridge pio run -d extras/bench_ap_bridge` and flash only its app (`write_flash 0xe000 ota_data_initial.bin 0x10000 firmware.bin`; same partition table, the board's NVS stays). Then, with Windows Python:

```bat
python ap_upload.py COM3 --key-file deploy.key --node 7680 --status
python ap_upload.py COM3 --key-file deploy.key --node 7680 --bundle bundle.bin [--cut-at 0.5]
```

If the node needs a mesh frame for its self-test (it has seen the mesh before), flash the bridge board back to LoRaChat right after a successful upload; the self-test waits at least 120 s.

## Signed commands

Commands with the Signed permission (`/maint.open`, `/reboot`, `/maint.wifi`, ...) only run over LoRa or MQTT with a signature made with the deployment key:

```bash
python3 scripts/ota_tools/lmcmd.py sign --key-file deploy.key --node 7680 "/maint.open 600 ap"
# @7680 /maint.open 600 ap #6526b3c0.1f2e...   <- type this on a gateway's (or any node's) serial console
python3 scripts/ota_tools/lmcmd.py sign --key-file deploy.key --node 7680 --mqtt 12 "/reboot"
# 7680 12 /reboot #6526b3c1.9a0b...            <- publish this on cmd/<gateway>
```

The tag is `HMAC-SHA256(K, "LMC1" || node u16 LE || counter u32 LE || line)[:8]`, so a signed line only runs on the node it was made for. The counter is `max(last + 1, unix time)`; `lmcmd.py` remembers the last one per key in `~/.config/loramesher/command_counters.json`, so several computers can sign with the same key.

WiFi credentials for a node (gateway WiFi and maintenance windows), encrypted for that node and that counter so the password never travels in clear over LoRa or MQTT:

```bash
python3 scripts/ota_tools/lmcmd.py wifi --key-file deploy.key --node 7680 --ssid MyHotspot
# asks for the password, prints "@7680 /maint.wifi <encrypted> #<counter>.<tag>"
```

The keystream is `HMAC-SHA256(Kw, node || counter || block)` with `Kw = HMAC-SHA256(K, "LMW1")`, and the tag covers the encrypted line. At SF9 one LoRa frame fits SSID and password up to about 37 bytes together. `lmcmd.py vectors` regenerates `test/vectors/cmdVectors.h`.

## Fault images

Fault images (`TEST_IMAGE_KIND` 1 crash, 2 irq-hang, 3 loop-hang, 4 self-test fail) are built at the next patch version and signed with the test key:

```bash
scripts/ota_tools/build_test_images.sh        # all kinds, or e.g. "build_test_images.sh 4"
```

They land in `release/tbeam/<version>-t<kind>/`. The script reuses the `tbeam` build directory, so the next normal build recompiles, mostly from the build cache.

## Floor image and provisioning

The floor image is the release every field node gets over USB; a failed update always rolls back to it, so it must already contain the boot guard, the blacklist and the reports. Collect it with `release.py`, then for each node, with Windows Python:

```bat
python flash_floor.py D:\path\to\release\tbeam\0.2.0+gabc1234 --port COM6 --key-file deploy.key ^
    --wifi-ssid FarmNet --role sensor --inventory nodes.csv
```

It writes bootloader, partition table, an erased NVS (`--keep-nvs` keeps it), erased `otadata` and the image in `app0`, resets the node once, waits for its `BOOT` line, then sends `/key.set` (and checks the fingerprint), `/maint.wifi` (asks for the password) and `/role.set`. Each node adds a row to the inventory: MAC, node address, version, bootloader hash, key fingerprint, AP name and the WiFi QR text for its sticker. The QR text contains the AP password, so keep the inventory like the key.

## Guards

`python3 scripts/ota_tools/check_guards.py` fails when:

- `esp_ota_mark_app_valid_cancel_rollback()` is called outside `src/ota/otaBootGuard.cpp`;
- `verifyRollbackLater()` is missing or does not return true;
- an sdkconfig enables `CONFIG_ARDUINO_ISR_IRAM`, lacks app rollback, or sets `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE`;
- `sdkconfig.tbeam-debug` differs from `sdkconfig.tbeam` in anything but heap and log debug options.

The earlier delta OTA over MQTT (patch generation, chunking, campaign and monitor tools) is kept in `extras/legacy_delta_ota/` for the planned delta OTA over LoRa.
