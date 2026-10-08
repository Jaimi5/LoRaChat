import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import check_guards  # noqa: E402

GOOD_MAIN = 'extern "C" bool verifyRollbackLater() {\n    return true;\n}\nvoid setup() {}\n'
GOOD_SDKCONFIG = "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n# CONFIG_ARDUINO_ISR_IRAM is not set\n"


def make_tree(tmp_path, main=GOOD_MAIN, guard="esp_ota_mark_app_valid_cancel_rollback();\n",
              sdkconfig=GOOD_SDKCONFIG):
    (tmp_path / "src" / "ota").mkdir(parents=True)
    (tmp_path / "src" / "main.cpp").write_text(main)
    (tmp_path / "src" / "ota" / "otaBootGuard.cpp").write_text(guard)
    (tmp_path / "sdkconfig.board").write_text(sdkconfig)
    return tmp_path


def test_clean_tree_passes(tmp_path):
    assert check_guards.run(make_tree(tmp_path)) == []


def test_mark_valid_outside_guard_fails(tmp_path):
    tree = make_tree(tmp_path, main=GOOD_MAIN + "esp_ota_mark_app_valid_cancel_rollback();\n")
    errors = check_guards.run(tree)
    assert any("src/main.cpp:5" in e for e in errors)


def test_missing_verify_rollback_later_fails(tmp_path):
    errors = check_guards.run(make_tree(tmp_path, main="void setup() {}\n"))
    assert any("verifyRollbackLater" in e for e in errors)


def test_isr_iram_fails(tmp_path):
    tree = make_tree(tmp_path, sdkconfig=GOOD_SDKCONFIG + "CONFIG_ARDUINO_ISR_IRAM=y\n")
    assert any("ARDUINO_ISR_IRAM" in e for e in check_guards.run(tree))


def test_missing_rollback_config_fails(tmp_path):
    tree = make_tree(tmp_path, sdkconfig="# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set\n")
    assert any("APP_ROLLBACK" in e for e in check_guards.run(tree))


def test_wdt_disable_in_user_code_fails(tmp_path):
    tree = make_tree(
        tmp_path, sdkconfig=GOOD_SDKCONFIG + "CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y\n")
    assert any("WDT_DISABLE" in e for e in check_guards.run(tree))
