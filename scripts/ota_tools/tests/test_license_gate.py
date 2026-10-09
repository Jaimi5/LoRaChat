import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import license_gate as lg  # noqa: E402

MIT_TEXT = "The MIT License (MIT)\n\nPermission is hereby granted, free of charge, ..."
BSD_TEXT = "Software License Agreement (BSD License)\n\nRedistribution and use in source ..."
GPL_TEXT = "GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007"
LGPL_TEXT = "GNU LESSER GENERAL PUBLIC LICENSE\nVersion 2.1, February 1999"
AGPL_TEXT = "GNU AFFERO GENERAL PUBLIC LICENSE\nVersion 3, 19 November 2007"


def library(root, name, json_license=None, properties_license=None, license_text=None,
            license_file="LICENSE"):
    path = root / name
    path.mkdir(parents=True)
    if json_license is not None:
        (path / "library.json").write_text(json.dumps({"name": name, "license": json_license}))
    if properties_license is not None:
        (path / "library.properties").write_text(f"name={name}\nlicense={properties_license}\n")
    if license_text is not None:
        (path / license_file).write_text(license_text)
    return path


def test_reads_the_declared_license_first(tmp_path):
    library(tmp_path, "A", json_license="MIT", license_text=GPL_TEXT)
    library(tmp_path, "B", properties_license="BSD-3-Clause")
    assert lg.find_licenses(tmp_path) == {"A": "MIT", "B": "BSD-3-Clause"}


def test_recognises_license_files(tmp_path):
    library(tmp_path, "Mit", license_text=MIT_TEXT)
    library(tmp_path, "Bsd", license_text=BSD_TEXT, license_file="license.txt")
    library(tmp_path, "Gpl", license_text=GPL_TEXT, license_file="COPYING")
    library(tmp_path, "Lgpl", license_text=LGPL_TEXT)
    library(tmp_path, "Agpl", license_text=AGPL_TEXT)
    library(tmp_path, "None")
    assert lg.find_licenses(tmp_path) == {"Mit": "MIT", "Bsd": "BSD", "Gpl": "GPL",
                                          "Lgpl": "LGPL", "Agpl": "AGPL", "None": "unknown"}


def test_copyleft_and_unknown_fail_unless_allowed(tmp_path):
    licenses = {"Mit": "MIT", "Gpl": "GPL-3.0", "Lgpl": "LGPL-2.1", "Agpl": "AGPL",
                "Odd": "unknown", "Core": "LGPL-2.1"}
    problems = lg.check(licenses, allow={"Core"})
    assert sorted(problems) == ["Agpl", "Gpl", "Lgpl", "Odd"]


def test_main_passes_on_permissive_libraries(tmp_path, capsys):
    library(tmp_path, "A", json_license="MIT")
    library(tmp_path, "B", license_text=BSD_TEXT)
    assert lg.main([str(tmp_path)]) == 0
    assert "A: MIT" in capsys.readouterr().out


def test_main_fails_and_names_the_library(tmp_path, capsys):
    library(tmp_path, "A", json_license="MIT")
    library(tmp_path, "Bad", license_text=GPL_TEXT)
    assert lg.main([str(tmp_path)]) == 1
    assert "Bad" in capsys.readouterr().err
    assert lg.main([str(tmp_path), "--allow", "Bad"]) == 0
