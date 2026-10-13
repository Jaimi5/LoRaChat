import json
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ppk2_compare as pc  # noqa: E402


def report(path, avg_ma, voltage=5.0, period=None, profile=None):
    data = {"avg_ma": avg_ma, "voltage": voltage, "avg_mw": avg_ma * voltage,
            "duration_s": 900.0, "charge_mah": avg_ma * 0.25,
            "battery": {"days": 3000 * 3.7 * 0.8 / (avg_ma * voltage) / 24}}
    if period:
        data["period_s"] = period
        data["profile_step_s"] = period / len(profile)
        data["profile_ma"] = profile
    path.write_text(json.dumps(data))
    return path


def test_campaign_rows_keep_order_and_groups(tmp_path):
    report(tmp_path / "a.json", 32.0, period=15.2, profile=[22.0, 34.0, 150.0, 22.0])
    report(tmp_path / "b.json", 8.0, period=15.2, profile=[2.0, 34.0, 150.0, 2.0])
    report(tmp_path / "c.json", 35.0)
    campaign = tmp_path / "campaign.json"
    campaign.write_text(json.dumps([
        {"group": "sleep", "label": "none", "report": "a.json"},
        {"group": "sleep", "label": "light", "report": "b.json"},
        {"group": "tests", "label": "boot", "report": "c.json"},
    ]))
    rows = pc.load_campaign(campaign)
    assert [(row["group"], row["label"]) for row in rows] == [
        ("sleep", "none"), ("sleep", "light"), ("tests", "boot")]
    assert rows[1]["avg_mw"] == pytest.approx(40.0)


def test_missing_report_is_skipped_with_a_note(tmp_path, capsys):
    campaign = tmp_path / "campaign.json"
    campaign.write_text(json.dumps([{"group": "g", "label": "x", "report": "nope.json"}]))
    assert pc.load_campaign(campaign) == []
    assert "nope.json" in capsys.readouterr().err


def test_saving_relative_to_the_first_row():
    rows = [{"label": "none", "avg_mw": 160.0}, {"label": "light", "avg_mw": 40.0}]
    assert pc.saving(rows) == [pytest.approx(0.0), pytest.approx(75.0)]


def test_figures_and_table_are_written(tmp_path):
    report(tmp_path / "a.json", 32.0, period=15.2, profile=[22.0, 34.0, 150.0, 22.0])
    report(tmp_path / "b.json", 8.0, period=30.4, profile=[2.0] * 6 + [150.0, 2.0])
    campaign = tmp_path / "campaign.json"
    campaign.write_text(json.dumps([
        {"group": "sleep", "label": "none", "report": "a.json"},
        {"group": "sleep", "label": "light", "report": "b.json"},
    ]))
    assert pc.main([str(campaign), "--out", str(tmp_path / "out")]) == 0
    assert (tmp_path / "out" / "sleep.png").stat().st_size > 0
    table = (tmp_path / "out" / "summary.md").read_text()
    assert "| sleep | light | 8.00 | 40.0 |" in table
    assert "75 %" in table


def test_profiles_are_aligned_on_their_transmission():
    profile = [1.0, 150.0, 30.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    aligned = pc.align(profile, at=0.8)
    assert aligned.index(150.0) == 8
    assert sorted(aligned) == sorted(profile)
