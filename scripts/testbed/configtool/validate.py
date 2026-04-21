"""Schema-check an experiment YAML."""

from __future__ import annotations

from pathlib import Path

from common import flatten, load_testbed_conf, load_yaml
from schema import PARAMS, param


def validate(yaml_path: Path, strict_devices: bool = False) -> list[str]:
    """Return a list of human-readable error strings. Empty list = valid.

    If `strict_devices` is true, every device in the YAML must exist in
    testbed.conf DEVICES, and warnings are emitted for DEVICES entries missing
    from the YAML.
    """
    errors: list[str] = []
    try:
        experiment = load_yaml(yaml_path)
    except Exception as e:
        return [f"{yaml_path}: failed to parse YAML: {e}"]

    allowed_top = {"experiment", "description", "defaults", "devices"}
    for k in experiment:
        if k not in allowed_top:
            errors.append(f"unknown top-level key: {k!r} (allowed: {sorted(allowed_top)})")

    for section in ("defaults", "devices"):
        sub = experiment.get(section)
        if sub is not None and not isinstance(sub, dict):
            errors.append(f"{section!r} must be a mapping, got {type(sub).__name__}")

    defaults = experiment.get("defaults") or {}
    for k, v in defaults.items():
        errors.extend(_validate_param(f"defaults.{k}", k, v))

    devices = experiment.get("devices") or {}
    for short_id, overrides in devices.items():
        # All-digit device IDs (e.g. "1484") get parsed by YAML as ints; coerce.
        sid = str(short_id)
        if overrides is None:
            continue
        if not isinstance(overrides, dict):
            errors.append(f"devices.{sid}: must be a mapping, got {type(overrides).__name__}")
            continue
        for k, v in overrides.items():
            errors.extend(_validate_param(f"devices.{sid}.{k}", k, v))

    # Optional: cross-check against testbed.conf.
    if strict_devices:
        try:
            conf = load_testbed_conf()
        except Exception as e:
            errors.append(f"could not load testbed.conf for strict check: {e}")
        else:
            known = set(conf.short_ids())
            yaml_ids = {str(k) for k in devices}
            for sid in yaml_ids - known:
                errors.append(f"device {sid!r} not in testbed.conf DEVICES")
            for sid in known - yaml_ids:
                errors.append(f"warning: {sid!r} in testbed.conf but missing from YAML")

    return errors


def _validate_param(path: str, key: str, value) -> list[str]:
    if key not in PARAMS:
        known = ", ".join(sorted(PARAMS))
        return [f"{path}: unknown parameter {key!r}. Known: {known}"]
    try:
        param(key).format_c(value)
    except Exception as e:
        return [f"{path}: invalid value {value!r} for {key} ({PARAMS[key].kind}): {e}"]
    return []
