import json
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import third_party as tp  # noqa: E402

MIT_TEXT = "The MIT License (MIT)\n\nPermission is hereby granted, free of charge, to any person"
BSD_TEXT = ("Software License Agreement (BSD License)\n\nRedistribution and use in source and "
            "binary forms, with or without modification, are permitted")
APACHE_TEXT = "Apache License\nVersion 2.0, January 2004\nhttp://www.apache.org/licenses/"
LGPL_TEXT = "GNU LESSER GENERAL PUBLIC LICENSE\nVersion 2.1, February 1999"
GPL_TEXT = "GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007"


def json_lib(root, name, license=None, url=None, version="1.0.0", files=None):
    lib = root / name
    lib.mkdir(parents=True)
    meta = {"name": name, "version": version}
    if license:
        meta["license"] = license
    if url:
        meta["repository"] = {"type": "git", "url": url}
    (lib / "library.json").write_text(json.dumps(meta))
    for file_name, text in (files or {}).items():
        (lib / file_name).write_text(text)
    return lib


def properties_lib(root, name, url, files):
    lib = root / name
    lib.mkdir(parents=True)
    (lib / "library.properties").write_text(f"name={name}\nversion=2.0.0\nurl={url}\n")
    for file_name, text in files.items():
        (lib / file_name).write_text(text)
    return lib


@pytest.mark.parametrize("text, expected", [
    (MIT_TEXT, "MIT"), (BSD_TEXT, "BSD"), (APACHE_TEXT, "Apache-2.0"),
    (LGPL_TEXT, "LGPL-2.1"), (GPL_TEXT, "GPL-3.0"), ("All rights reserved.", None),
])
def test_detects_the_license_from_its_text(text, expected):
    assert tp.detect_license(text) == expected


def test_scan_reads_library_json_properties_and_license_files(tmp_path):
    json_lib(tmp_path, "RadioLib", "MIT", "https://github.com/jgromes/RadioLib.git", "7.8.1")
    json_lib(tmp_path, "ArduinoJson", url="https://github.com/bblanchon/ArduinoJson.git",
             files={"LICENSE.txt": MIT_TEXT})
    properties_lib(tmp_path, "Adafruit GFX Library",
                   "https://github.com/adafruit/Adafruit-GFX-Library", {"license.txt": BSD_TEXT})
    (tmp_path / "integrity.dat").write_text("")

    libs = {lib.name: lib for lib in tp.scan(tmp_path)}
    assert set(libs) == {"RadioLib", "ArduinoJson", "Adafruit GFX Library"}
    assert libs["RadioLib"].license == "MIT"
    assert libs["RadioLib"].version == "7.8.1"
    assert libs["RadioLib"].url == "https://github.com/jgromes/RadioLib"
    assert libs["ArduinoJson"].license == "MIT"
    assert libs["Adafruit GFX Library"].license == "BSD"
    assert libs["Adafruit GFX Library"].version == "2.0.0"


def test_license_gate_allows_permissive_and_allowlisted_lgpl_only(tmp_path):
    libs = [
        tp.Library("RadioLib", "1", "MIT", "u1"),
        tp.Library("Adafruit BusIO", "1", "BSD", "u2"),
        tp.Library("TinyGPSPlus", "1", "LGPL-2.1", "u3"),
        tp.Library("SomeGpl", "1", "GPL-3.0", "u4"),
        tp.Library("SomeLgpl", "1", "LGPL-3.0", "u5"),
        tp.Library("Mystery", "1", None, "u6"),
    ]
    errors = tp.check_licenses(libs)
    assert len(errors) == 3
    assert any("SomeGpl" in e for e in errors)
    assert any("SomeLgpl" in e for e in errors)
    assert any("Mystery" in e and "unknown" in e for e in errors)


README = """# Intro

# Acknowledgements

| Project | Author | License | Used for |
|---|---|---|---|
| [RadioLib](https://github.com/jgromes/RadioLib) | Jan | MIT | Radio |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library), [BusIO](https://github.com/adafruit/Adafruit_BusIO) | Adafruit | BSD / MIT | OLED |

# Disclaimer

[Other](https://github.com/example/Other) | MIT
"""


def test_readme_check_needs_every_library_with_its_license_family():
    libs = [
        tp.Library("RadioLib", "1", "MIT", "https://github.com/jgromes/RadioLib"),
        tp.Library("Adafruit BusIO", "1", "BSD", "https://github.com/adafruit/Adafruit_BusIO"),
        tp.Library("Other", "1", "MIT", "https://github.com/example/Other"),
        tp.Library("Wrong", "1", "Apache-2.0", "https://github.com/jgromes/RadioLib"),
    ]
    errors = tp.check_readme(libs, README)
    assert len(errors) == 2
    assert any("Other" in e and "missing" in e for e in errors)
    assert any("Wrong" in e and "Apache" in e for e in errors)


def test_readme_check_fails_without_the_section():
    libs = [tp.Library("RadioLib", "1", "MIT", "https://github.com/jgromes/RadioLib")]
    assert tp.check_readme(libs, "# Intro\n") == ["README has no '# Acknowledgements' section"]


def test_main_checks_a_libdeps_folder(tmp_path, capsys):
    libdeps = tmp_path / "libdeps"
    json_lib(libdeps, "RadioLib", "MIT", "https://github.com/jgromes/RadioLib.git")
    readme = tmp_path / "README.md"
    readme.write_text(README)
    assert tp.main(["check", "--libdeps", str(libdeps), "--readme", str(readme)]) == 0
    assert "RadioLib" in capsys.readouterr().out

    json_lib(libdeps, "SomeGpl", "GPL-3.0", "https://github.com/example/SomeGpl")
    assert tp.main(["check", "--libdeps", str(libdeps), "--readme", str(readme)]) == 1


def test_main_fails_when_the_libraries_are_not_resolved(tmp_path):
    assert tp.main(["check", "--libdeps", str(tmp_path / "missing")]) == 1
