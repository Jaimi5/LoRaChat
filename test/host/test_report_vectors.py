"""Checks test/vectors/reportVectors.h against the server's report vectors.

The node report is checked by LoRaMesherWeb (web/src/lib/ota); its shared vectors live in that
repository at ota/vectors/report_vectors.json. The test is skipped when the repository is not
next to this one.
"""
import json
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "test" / "vectors" / "reportVectors.h"
SERVER_VECTORS = ROOT.parents[2] / "LMWeb" / "LoRaMesherWeb" / "ota" / "vectors" / "report_vectors.json"


def constant(text: str, name: str) -> str:
    match = re.search(name + r' =\s*("(?:[^"\\]|\\.)*");', text)
    assert match, name
    return json.loads(match.group(1))


@pytest.mark.skipif(not SERVER_VECTORS.exists(), reason="LoRaMesherWeb not checked out next to LoRaChat")
def test_header_matches_the_server_vectors():
    vectors = json.loads(SERVER_VECTORS.read_text())
    cases = {case["name"]: case for case in vectors["cases"]}
    header = HEADER.read_text()
    assert constant(header, "REPORT_KEY_HEX") == vectors["report_key"]
    assert constant(header, "REPORT_NOOP_BODY") == cases["noop"]["body"]
    assert constant(header, "REPORT_NOOP_TAG") == cases["noop"]["tag"]
    assert constant(header, "REPORT_ROLLBACK_BODY") == cases["rollback"]["body"]
