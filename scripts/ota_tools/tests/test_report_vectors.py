import hashlib
import hmac
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import report_vectors as rv  # noqa: E402

VECTORS = json.loads(rv.VECTORS_JSON.read_text())


def test_header_is_up_to_date():
    assert rv.VECTORS_HEADER.read_text() == rv.vectors_header(VECTORS)


def test_report_key_is_the_labelled_hmac():
    key = bytes.fromhex(VECTORS["deployment_key"])
    assert rv.report_key(key) == hmac.new(key, b"LMR1", hashlib.sha256).digest()
    assert rv.report_key(key).hex() == VECTORS["report_key"]


def test_vector_tags_verify_only_when_valid():
    kr = bytes.fromhex(VECTORS["report_key"])
    for case in VECTORS["cases"]:
        expected = hmac.new(kr, case["body"].encode(), hashlib.sha256).hexdigest()
        assert hmac.compare_digest(expected, case["tag"]) == case["valid"], case["name"]


def test_key_command_prints_the_report_key(tmp_path, capsys):
    key_file = tmp_path / "deploy.key"
    key_file.write_text(VECTORS["deployment_key"] + "\n")
    assert rv.main(["key", "--key-file", str(key_file)]) == 0
    assert capsys.readouterr().out.strip() == VECTORS["report_key"]


def test_key_command_rejects_a_short_key(tmp_path):
    key_file = tmp_path / "deploy.key"
    key_file.write_text("00" * 16)
    assert rv.main(["key", "--key-file", str(key_file)]) == 1
