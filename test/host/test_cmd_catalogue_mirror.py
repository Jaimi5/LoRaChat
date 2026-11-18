"""Keeps the catalogue fixture of the native tests in step with the firmware.

test/test_cmd_catalogue builds the /cmds pages of a gateway from a copy of the command
registrations, since the registrations themselves need Arduino. This test reads the sources in
src/ and fails when a command name or the text of a specific error differs from that copy.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "test" / "test_cmd_catalogue" / "test_main.cpp"

REGISTRATION = re.compile(r'(?:\bCommand|\bstoredSetting|\badd)\(\s*(?:\*this,\s*)?"(/[A-Za-z.]+)"')
CODE_TEXT = re.compile(r'\.withCode\(\s*\w+,\s*((?:"[^"]*"\s*)+)\)')
LITERAL = re.compile(r'"([^"]*)"')


def join_literals(text: str) -> str:
    return "".join(LITERAL.findall(text))


def firmware_sources() -> str:
    return "\n".join(path.read_text() for path in sorted((ROOT / "src").rglob("*.cpp")))


def fixture_body() -> str:
    text = FIXTURE.read_text()
    start = text.index("CommandCatalogue firmwareCatalogue()")
    return text[start:text.index("}  // namespace", start)]


def test_fixture_lists_the_commands_of_the_firmware():
    registered = sorted(REGISTRATION.findall(firmware_sources()))
    assert len(registered) == len(set(registered)), "a command is registered twice in src/"
    fixture = sorted(re.findall(r'spec\("(/[A-Za-z.]+)"', fixture_body()))
    assert fixture == registered


def test_fixture_has_the_texts_of_the_specific_errors():
    # Adjacent string literals are joined before comparing.
    fixture = join_literals(fixture_body())
    texts = [join_literals(match) for match in CODE_TEXT.findall(firmware_sources())]
    assert texts, "no specific error found in src/"
    missing = [text for text in texts if text not in fixture]
    assert not missing
