"""Legacy bench tool: run GPS checks against the disconnected Uno diagnostic."""

from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime, timezone

import serial


FIELD_RE = re.compile(r"^\s*([a-z_]+)=(.*)\s*$")


def parse_bool(value: str) -> bool:
    return value.strip().lower() == "true"


def parse_utc(value: str) -> datetime:
    return datetime.fromisoformat(value.strip().replace("Z", "+00:00"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--minutes", type=float, default=15.0)
    parser.add_argument("--interval", type=float, default=30.0)
    args = parser.parse_args()

    started = time.monotonic()
    deadline = started + args.minutes * 60.0
    next_request = started + 8.0  # Allow the Uno's automatic boot test to finish.
    samples: list[dict[str, object]] = []
    current: dict[str, object] | None = None

    print(
        f"GPS_SOAK_START port={args.port} minutes={args.minutes:g} "
        f"interval_s={args.interval:g}",
        flush=True,
    )

    with serial.Serial(args.port, 115200, timeout=0.25) as connection:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_request:
                connection.write(b"g\n")
                connection.flush()
                next_request += args.interval

            raw = connection.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if line == "[GPS NMEA via AltSoftSerial]":
                current = {"monotonic": time.monotonic()}
                continue
            if current is None:
                continue
            if line.startswith("PASS:") or line.startswith("FAIL:"):
                current["result"] = "PASS" if line.startswith("PASS:") else "FAIL"
                samples.append(current)
                utc = current.get("utc", "unavailable")
                print(
                    "GPS_SAMPLE "
                    f"n={len(samples)} elapsed_s={time.monotonic() - started:.1f} "
                    f"result={current['result']} chars={current.get('new_chars')} "
                    f"ok={current.get('new_sentences_checksum_ok')} "
                    f"bad={current.get('new_sentences_checksum_failed')} "
                    f"time_valid={current.get('time_valid')} "
                    f"fix_valid={current.get('fix_valid')} "
                    f"satellites={current.get('satellites')} utc={utc}",
                    flush=True,
                )
                current = None
                continue
            match = FIELD_RE.match(line)
            if not match:
                continue
            name, value = match.groups()
            if name in {"fix_valid", "time_valid"}:
                current[name] = parse_bool(value)
            elif name == "utc":
                # The diagnostic prints UTC and its date/time ages on one line.
                current[name] = value.split()[0]
            elif name in {
                "new_chars",
                "new_sentences_checksum_ok",
                "new_sentences_checksum_failed",
                "total_chars",
                "total_sentences_checksum_ok",
                "satellites",
            }:
                try:
                    current[name] = int(value)
                except ValueError:
                    current[name] = value

    failures = [sample for sample in samples if sample.get("result") != "PASS"]
    invalid_time = [sample for sample in samples if sample.get("time_valid") is not True]
    invalid_fix = [sample for sample in samples if sample.get("fix_valid") is not True]
    weak_windows = [
        sample
        for sample in samples
        if int(sample.get("new_chars", 0)) < 20
        or int(sample.get("new_sentences_checksum_ok", 0)) < 1
    ]
    checksum_failures = sum(
        int(sample.get("new_sentences_checksum_failed", 0)) for sample in samples
    )

    time_jumps: list[tuple[int, float, float]] = []
    timed_samples: list[tuple[int, dict[str, object], datetime]] = []
    for index, sample in enumerate(samples, start=1):
        value = sample.get("utc")
        if not isinstance(value, str) or value == "unavailable":
            continue
        try:
            timed_samples.append((index, sample, parse_utc(value)))
        except ValueError:
            time_jumps.append((index, float("nan"), float("nan")))
    for previous, current_sample in zip(timed_samples, timed_samples[1:]):
        previous_index, previous_data, previous_utc = previous
        current_index, current_data, current_utc = current_sample
        gps_delta = (current_utc - previous_utc).total_seconds()
        monotonic_delta = float(current_data["monotonic"]) - float(
            previous_data["monotonic"]
        )
        if abs(gps_delta - monotonic_delta) > 3.0:
            time_jumps.append((current_index, gps_delta, monotonic_delta))

    satellites = [
        int(sample["satellites"])
        for sample in samples
        if isinstance(sample.get("satellites"), int)
    ]
    elapsed = time.monotonic() - started
    print(
        "GPS_SOAK_SUMMARY "
        f"elapsed_s={elapsed:.1f} samples={len(samples)} failures={len(failures)} "
        f"weak_windows={len(weak_windows)} invalid_time={len(invalid_time)} "
        f"invalid_fix={len(invalid_fix)} checksum_failures={checksum_failures} "
        f"time_jumps={len(time_jumps)} "
        f"satellites_min={min(satellites) if satellites else 'n/a'} "
        f"satellites_max={max(satellites) if satellites else 'n/a'} "
        f"finished_utc={datetime.now(timezone.utc).isoformat()}",
        flush=True,
    )
    for index, gps_delta, monotonic_delta in time_jumps:
        print(
            f"GPS_TIME_JUMP sample={index} gps_delta_s={gps_delta} "
            f"monotonic_delta_s={monotonic_delta:.3f}",
            flush=True,
        )

    return 0 if samples and not failures and not weak_windows and not invalid_time and not time_jumps else 1


if __name__ == "__main__":
    sys.exit(main())
