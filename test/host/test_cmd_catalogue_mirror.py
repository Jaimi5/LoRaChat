"""Keeps the catalogue fixture of the native tests in step with the firmware.

test/test_cmd_catalogue builds the /cmds pages of a gateway from a copy of the command
registrations, since the registrations themselves need Arduino. This test reads the sources in
src/ and fails when a command name, the text or the detail of a specific error, a command id, a
schema or a reply template differs from that copy. The ids are also compared with the golden file
test/vectors/cmdIds.txt, which the native tests check against the same copy.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "test" / "fakes" / "firmwareCatalogue.h"
GOLDEN_IDS = ROOT / "test" / "vectors" / "cmdIds.txt"

REGISTRATION = re.compile(
    r'(?:\bCommand|\bstoredSetting|\badd)\(\s*(?:\*this,\s*)?(?:0x[0-9A-Fa-f]{2},\s*)?'
    r'"(/[A-Za-z.]+)"')
TYPED_REGISTRATION = re.compile(
    r'(?:\bCommand|\bstoredSetting)\(\s*0x([0-9A-Fa-f]{2}),\s*"(/[A-Za-z.]+)"')
TYPED_FIXTURE = re.compile(r'\btyped\(\s*0x([0-9A-Fa-f]{2}),\s*spec\("(/[A-Za-z.]+)"')
LITERALS = r'((?:"[^"]*"\s*)+)'
ARGS_SCHEMA = re.compile(r'\.args\(\s*' + LITERALS + r'\)')
REPLY = re.compile(r'\.reply\(\s*' + LITERALS + r',\s*' + LITERALS + r'\)')
CODE_TEXT = re.compile(r'\.withCode\(\s*\w+,\s*((?:"[^"]*"\s*)+)(?:,\s*("[^"]*")\s*)?\)')
FIXTURE_CODE = re.compile(r'\{\d+,\s*((?:"[^"]*"\s*)+)(?:,\s*("[^"]*")\s*)?\}')
LITERAL = re.compile(r'"([^"]*)"')


def join_literals(text: str) -> str:
    return "".join(LITERAL.findall(text))


def firmware_sources() -> str:
    return "\n".join(path.read_text() for path in sorted((ROOT / "src").rglob("*.cpp")))


def fixture_body() -> str:
    text = FIXTURE.read_text()
    start = text.index("CommandCatalogue firmwareCatalogue()")
    return text[start:text.index("\n}\n", start)]


def id_pairs(matches) -> list:
    return sorted(f"{int(code, 16):02x}={name}" for code, name in matches)


def test_fixture_lists_the_commands_of_the_firmware():
    registered = sorted(REGISTRATION.findall(firmware_sources()))
    assert len(registered) == len(set(registered)), "a command is registered twice in src/"
    fixture = sorted(re.findall(r'spec\("(/[A-Za-z.]+)"', fixture_body()))
    assert fixture == registered


def test_fixture_has_the_texts_of_the_specific_errors():
    # Adjacent string literals are joined before comparing. Each error is compared with its
    # detail, the way its number reads as text.
    def pairs(matches):
        return {(join_literals(text), join_literals(detail)) for text, detail in matches}

    declared = pairs(CODE_TEXT.findall(firmware_sources()))
    assert declared, "no specific error found in src/"
    assert any(detail for _, detail in declared), "no error with a detail found in src/"
    assert declared == pairs(FIXTURE_CODE.findall(fixture_body()))


def test_ids_match_the_golden_file_and_the_fixture():
    golden = GOLDEN_IDS.read_text().split()
    assert golden == sorted(golden), "the golden file is sorted by id"
    assert id_pairs(TYPED_REGISTRATION.findall(firmware_sources())) == golden
    assert id_pairs(TYPED_FIXTURE.findall(fixture_body())) == golden


def test_fixture_has_the_schemas_and_templates():
    fixture = join_literals(fixture_body())
    sources = firmware_sources()
    declared = [join_literals(match) for match in ARGS_SCHEMA.findall(sources)]
    replies = REPLY.findall(sources)
    assert replies, "no typed reply found in src/"
    for schema, template in replies:
        declared += [join_literals(schema), join_literals(template)]
    missing = [text for text in declared if text not in fixture]
    assert not missing
