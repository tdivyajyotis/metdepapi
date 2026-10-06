from __future__ import annotations

import csv
import hashlib
import importlib.util
import io
import json
import ssl
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
FIRMWARE = ROOT / "firmware" / "nodemcu-sensor-node"
GENERATOR_PATH = FIRMWARE / "tools" / "generate_ca_bundle.py"
SPEC = importlib.util.spec_from_file_location("generate_ca_bundle", GENERATOR_PATH)
assert SPEC and SPEC.loader
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def _archive_members(archive: bytes) -> list[bytes]:
    assert archive.startswith(b"!<arch>\n")
    members = []
    offset = 8
    while offset < len(archive):
        header = archive[offset : offset + 60]
        assert len(header) == 60
        assert header[58:60] == b"`\n"
        size = int(header[48:58].decode("ascii").strip())
        offset += 60
        members.append(archive[offset : offset + size])
        offset += size + size % 2
    assert offset == len(archive)
    return members


class CaBundleTests(unittest.TestCase):
    def test_committed_ca_bundle_matches_manifest(self) -> None:
        data = FIRMWARE / "data"
        archive = (data / "certs.ar").read_bytes()
        manifest = json.loads((data / "ca-bundle-manifest.json").read_text())

        members = _archive_members(archive)
        self.assertEqual(len(members), manifest["certificate_count"])
        self.assertEqual(len(members), len(manifest["certificates"]))
        self.assertEqual(
            hashlib.sha256(archive).hexdigest(), manifest["archive_sha256"]
        )
        self.assertEqual(
            [hashlib.sha256(member).hexdigest().upper() for member in members],
            [
                certificate["fingerprint_sha256"]
                for certificate in manifest["certificates"]
            ],
        )

    def test_generator_rejects_report_fingerprint_mismatch(self) -> None:
        header = (FIRMWARE / "include" / "gts_root_r4.h").read_text()
        begin = header.index("-----BEGIN CERTIFICATE-----")
        end = header.index("-----END CERTIFICATE-----") + len(
            "-----END CERTIFICATE-----"
        )
        pem = header[begin:end]

        output = io.StringIO(newline="")
        fieldnames = [
            "Common Name or Certificate Name",
            "SHA-256 Fingerprint",
            "Valid From [GMT]",
            "Valid To [GMT]",
            "Trust Bits",
            "Distrust for TLS After Date",
            "PEM Info",
        ]
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerow(
            {
                "Common Name or Certificate Name": "GTS Root R4",
                "SHA-256 Fingerprint": "00" * 32,
                "Valid From [GMT]": "2016.06.22",
                "Valid To [GMT]": "2036.06.22",
                "Trust Bits": "Websites",
                "Distrust for TLS After Date": "",
                "PEM Info": f"'{pem}'",
            }
        )

        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            GENERATOR.load_tls_roots(output.getvalue().encode())

        self.assertTrue(ssl.PEM_cert_to_DER_cert(pem))


if __name__ == "__main__":
    unittest.main()
