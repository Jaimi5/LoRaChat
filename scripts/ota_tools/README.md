# OTA tools

Host tools for the near-field WiFi OTA. They need only the Python standard library.

| Tool | Purpose |
|---|---|
| `check_guards.py` | Static checks that keep app rollback working (`mark_app_valid` only in the boot guard, `verifyRollbackLater()`, rollback-related sdkconfig options, debug sdkconfig variants in step with release). Run from the project root: `python3 scripts/ota_tools/check_guards.py` |
| `otadata.py` | Decodes the `otadata` partition and writes a copy that boots a chosen slot, optionally in state `NEW` for boot-guard tests. Used with `esptool read_flash 0xe000 0x2000` / `write_flash 0xe000` |

Tests: `python3 -m pytest scripts/ota_tools/tests test/host`.

The earlier delta OTA over MQTT (patch generation, chunking, campaign and monitor tools) is kept in `extras/legacy_delta_ota/` for the planned delta OTA over LoRa.
