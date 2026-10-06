from __future__ import annotations

import math
from collections.abc import Iterator
from typing import Any


def infer_unit(metric: str) -> str | None:
    """Infer common units from the firmware's explicit metric suffixes."""
    suffixes = {
        "_c": "degC",
        "_pct": "percent",
        "_v": "V",
        "_mv": "mV",
        "_dbm": "dBm",
        "_ms": "ms",
        "_nm": "nm",
        "_hpa": "hPa",
        "_mm": "mm",
        "_counts": "count",
    }
    leaf = metric.rsplit(".", 1)[-1].lower()
    for suffix, unit in suffixes.items():
        if leaf.endswith(suffix):
            return unit
    return None


def flatten_numeric(
    value: Any, prefix: str = ""
) -> Iterator[tuple[str, float, str | None]]:
    """Flatten numeric leaves while excluding booleans and non-finite values."""
    if isinstance(value, dict):
        for key, child in value.items():
            path = f"{prefix}.{key}" if prefix else str(key)
            yield from flatten_numeric(child, path)
        return
    if isinstance(value, list):
        for index, child in enumerate(value):
            path = f"{prefix}.{index}" if prefix else str(index)
            yield from flatten_numeric(child, path)
        return
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return
    number = float(value)
    if prefix and math.isfinite(number):
        yield prefix, number, infer_unit(prefix)

