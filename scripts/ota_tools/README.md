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

A sensor node checks the server in its boot window. Expected serial output: `Server offers 0.1.1+gabc1234 ...`, `Installing ...`, `Image OK after 1 attempt(s): ... B in ... ms (flash ... ms)`, `Rebooting into ... on app1`, then the boot guard lines above. The server logs every request. Faults for the bench cases: `--flip-byte N` (one byte changed: `SHA_MISMATCH`, nothing installed), `--cut-at 0.5 [--cut-times K]` (connection closed at 50 %: up to 3 attempts, then `TRANSPORT_FAILED`), `--missing-image` (404). After a failure the node restarts its current image and skips that image until the next power-on.

Fault images (`TEST_IMAGE_KIND` 1 crash, 2 irq-hang, 3 loop-hang, 4 self-test fail) are built at the next patch version and signed with the test key:

```bash
scripts/ota_tools/build_test_images.sh        # all kinds, or e.g. "build_test_images.sh 4"
```

They land in `release/tbeam/<version>-t<kind>/`. The script reuses the `tbeam` build directory, so the next normal build recompiles, mostly from the build cache.

## Guards

`python3 scripts/ota_tools/check_guards.py` fails when:

- `esp_ota_mark_app_valid_cancel_rollback()` is called outside `src/ota/otaBootGuard.cpp`;
- `verifyRollbackLater()` is missing or does not return true;
- an sdkconfig enables `CONFIG_ARDUINO_ISR_IRAM`, lacks app rollback, or sets `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE`;
- `sdkconfig.tbeam-debug` differs from `sdkconfig.tbeam` in anything but heap and log debug options.

The earlier delta OTA over MQTT (patch generation, chunking, campaign and monitor tools) is kept in `extras/legacy_delta_ota/` for the planned delta OTA over LoRa.
