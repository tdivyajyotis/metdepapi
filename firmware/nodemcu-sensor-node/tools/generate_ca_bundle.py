#!/usr/bin/env python3
"""Build an ESP8266 BearSSL CertStore archive from Mozilla's CCADB report."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import ssl
import sys
import urllib.request
from datetime import date, datetime, timezone
from pathlib import Path


DEFAULT_SOURCE = (
    "https://ccadb.my.salesforce-sites.com/mozilla/"
    "IncludedCACertificateReportPEMCSV"
)
AR_MAGIC = b"!<arch>\n"


def read_source(source: str, input_path: Path | None) -> bytes:
    if input_path is not None:
        return input_path.read_bytes()
    request = urllib.request.Request(
        source, headers={"User-Agent": "metdepapi-ca-bundle/1.0"}
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def load_tls_roots(csv_bytes: bytes) -> list[dict[str, str | bytes]]:
    text = csv_bytes.decode("utf-8-sig")
    roots: list[dict[str, str | bytes]] = []
    seen: set[str] = set()
    for row in csv.DictReader(text.splitlines(keepends=True)):
        if "Websites" not in row.get("Trust Bits", ""):
            continue
        distrust_after = row.get("Distrust for TLS After Date", "").strip()
        if distrust_after:
            distrust_date = datetime.strptime(distrust_after, "%Y.%m.%d").date()
            if distrust_date <= date.today():
                continue
        valid_to = row.get("Valid To [GMT]", "").strip()
        if valid_to and datetime.strptime(valid_to, "%Y.%m.%d").date() <= date.today():
            continue
        pem = row.get("PEM Info", "").strip().strip("'")
        if not pem:
            continue
        der = ssl.PEM_cert_to_DER_cert(pem)
        fingerprint = hashlib.sha256(der).hexdigest().upper()
        reported = row.get("SHA-256 Fingerprint", "").replace(":", "").upper()
        if reported and reported != fingerprint:
            raise ValueError(
                f"fingerprint mismatch for {row.get('Common Name or Certificate Name')}"
            )
        if fingerprint in seen:
            continue
        seen.add(fingerprint)
        roots.append(
            {
                "name": row.get("Common Name or Certificate Name", "unnamed"),
                "fingerprint_sha256": fingerprint,
                "valid_from": row.get("Valid From [GMT]", ""),
                "valid_to": row.get("Valid To [GMT]", ""),
                "der": der,
            }
        )
    if not roots:
        raise ValueError("source contained no website-trusted roots")
    return roots


def ar_member(name: str, payload: bytes) -> bytes:
    encoded_name = f"{name}/".encode("ascii")
    if len(encoded_name) > 16:
        raise ValueError(f"archive member name is too long: {name}")
    header = b"".join(
        (
            encoded_name.ljust(16),
            b"0".ljust(12),
            b"0".ljust(6),
            b"0".ljust(6),
            b"100644".ljust(8),
            str(len(payload)).encode("ascii").ljust(10),
            b"`\n",
        )
    )
    if len(header) != 60:
        raise AssertionError("invalid ar member header")
    return header + payload + (b"\n" if len(payload) % 2 else b"")


def make_archive(roots: list[dict[str, str | bytes]]) -> bytes:
    members = [
        ar_member(f"ca_{index:03d}.der", root["der"])
        for index, root in enumerate(roots)
    ]
    return AR_MAGIC + b"".join(members)


def verify_archive(archive: bytes, roots: list[dict[str, str | bytes]]) -> None:
    if not archive.startswith(AR_MAGIC):
        raise ValueError("archive magic is invalid")
    offset = len(AR_MAGIC)
    extracted: list[bytes] = []
    while offset < len(archive):
        header = archive[offset : offset + 60]
        if len(header) != 60 or header[58:60] != b"`\n":
            raise ValueError("archive member header is invalid")
        size = int(header[48:58].decode("ascii").strip())
        offset += 60
        extracted.append(archive[offset : offset + size])
        offset += size + (size % 2)
    expected = [root["der"] for root in roots]
    if extracted != expected:
        raise ValueError("archive verification failed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--input", type=Path, help="use a previously downloaded CSV")
    parser.add_argument(
        "--output", type=Path, default=Path(__file__).parents[1] / "data" / "certs.ar"
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).parents[1] / "data" / "ca-bundle-manifest.json",
    )
    args = parser.parse_args()

    csv_bytes = read_source(args.source, args.input)
    roots = load_tls_roots(csv_bytes)
    archive = make_archive(roots)
    verify_archive(archive, roots)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(archive)
    manifest = {
        "source": args.source,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_sha256": hashlib.sha256(csv_bytes).hexdigest(),
        "archive_sha256": hashlib.sha256(archive).hexdigest(),
        "certificate_count": len(roots),
        "certificates": [
            {key: value for key, value in root.items() if key != "der"}
            for root in roots
        ],
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        f"wrote {len(roots)} TLS roots to {args.output} "
        f"({len(archive)} bytes, sha256={manifest['archive_sha256']})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
