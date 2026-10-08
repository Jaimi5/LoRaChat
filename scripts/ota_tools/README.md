# OTA tools

Host tools for the near-field WiFi OTA. They need only the Python standard library.

| Tool | Purpose |
|---|---|
| `version_hook.py` | PlatformIO `pre:` script of the `tbeam` envs. Sets the image version (`esp_app_desc_t.version`) from `version.txt`, or `FW_VERSION` if set, plus git metadata: `0.1.0+gabc1234`, with `.dirty` for uncommitted changes to tracked files. When the version changes it drops the CMake cache so ESP-IDF picks it up |
| `release.py` | Checks a build and copies its images to `release/<env>/<version>[-<tag>]/` with `info.json`. Checks the appended SHA-256, the 90 % size gate and that the image version matches `version.txt`; refuses `.dirty` builds unless `--allow-dirty`. `python3 scripts/ota_tools/release.py --env tbeam` |
| `build_test_images.sh` | Builds the boot-guard test images (`TEST_IMAGE_KIND` 1-4, next patch version) and collects them with `release.py --tag t<kind>`. It reuses the `tbeam` build directory, so the next normal build recompiles, from the cache |
| `check_guards.py` | Static checks that keep app rollback working (`mark_app_valid` only in the boot guard, `verifyRollbackLater()`, rollback-related sdkconfig options, debug sdkconfig variants in step with release). Run from the project root: `python3 scripts/ota_tools/check_guards.py` |
| `otadata.py` | Decodes the `otadata` partition and writes a copy that boots a chosen slot, optionally in state `NEW` for boot-guard tests. Used with `esptool read_flash 0xe000 0x2000` / `write_flash 0xe000` |

## Release flow

1. Set the version in `version.txt` (`MAJOR.MINOR.PATCH`, each 0-255) and commit.
2. `scripts/pio.sh run -e tbeam`
3. `python3 scripts/ota_tools/release.py --env tbeam` writes `release/tbeam/<version>/`.

The image identity used by the manifest, the blacklist and the reports is `image_sha256` in `info.json`: the SHA-256 that ESP-IDF appends to `firmware.bin`, equal to `esp_partition_get_sha256()` on the node. The manifest's numeric version is `version_u32` (`0xMMmmpp00`).

Flash offsets for a full USB flash: `bootloader.bin` 0x1000, `partitions.bin` 0x8000, `ota_data_initial.bin` 0xe000, `firmware.bin` 0x10000.

Tests: `python3 -m pytest scripts/ota_tools/tests test/host`.

The earlier delta OTA over MQTT (patch generation, chunking, campaign and monitor tools) is kept in `extras/legacy_delta_ota/` for the planned delta OTA over LoRa.
