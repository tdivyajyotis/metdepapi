import importlib.util
import unittest
from datetime import UTC, datetime
from unittest.mock import patch


SERVER_DEPS_AVAILABLE = bool(
    importlib.util.find_spec("fastapi") and importlib.util.find_spec("psycopg")
)

if SERVER_DEPS_AVAILABLE:
    from fastapi.testclient import TestClient

    from sensor_server import main


@unittest.skipUnless(SERVER_DEPS_AVAILABLE, "install the 'server' extra")
class ApiTests(unittest.TestCase):
    def setUp(self):
        self.initializer = patch.object(main.database, "initialize", return_value=None)
        self.initializer.start()
        self.client_context = TestClient(main.app)
        self.client = self.client_context.__enter__()

    def tearDown(self):
        self.client_context.__exit__(None, None, None)
        self.initializer.stop()

    @staticmethod
    def payload():
        return {
            "schema_version": 1,
            "device_id": "station-001",
            "event_id": "station-001-test-1",
            "sequence": 1,
            "observed_at": "2026-10-06T00:00:00Z",
            "firmware": "test",
            "sensors": {
                "arduino_adc": {
                    "ok": True,
                    "channels": [{"channel": 0, "raw_counts": 512}],
                }
            },
        }

    def test_ingest_requires_authentication(self):
        response = self.client.post("/v1/readings", json=self.payload())
        self.assertEqual(response.status_code, 401)

    def test_device_header_must_match_body(self):
        response = self.client.post(
            "/v1/readings",
            json=self.payload(),
            headers={"Authorization": "Bearer secret", "X-Device-ID": "other"},
        )
        self.assertEqual(response.status_code, 400)

    def test_valid_reading_is_accepted(self):
        result = {
            "accepted": True,
            "duplicate": False,
            "event_id": "station-001-test-1",
            "received_at": datetime(2026, 10, 6, tzinfo=UTC),
            "measurement_count": 2,
        }
        with (
            patch.object(main.database, "authenticate_device", return_value=True),
            patch.object(main.database, "insert_reading", return_value=result),
        ):
            response = self.client.post(
                "/v1/readings",
                json=self.payload(),
                headers={
                    "Authorization": "Bearer secret",
                    "X-Device-ID": "station-001",
                },
            )
        self.assertEqual(response.status_code, 201)
        self.assertFalse(response.json()["duplicate"])

    def test_duplicate_reading_returns_200(self):
        result = {
            "accepted": True,
            "duplicate": True,
            "event_id": "station-001-test-1",
            "received_at": datetime(2026, 10, 6, tzinfo=UTC),
            "measurement_count": 2,
        }
        with (
            patch.object(main.database, "authenticate_device", return_value=True),
            patch.object(main.database, "insert_reading", return_value=result),
        ):
            response = self.client.post(
                "/v1/readings",
                json=self.payload(),
                headers={"Authorization": "Bearer secret"},
            )
        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.json()["duplicate"])

    def test_payload_limit(self):
        response = self.client.post(
            "/v1/readings",
            content=b"x" * (main.settings.max_payload_bytes + 1),
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(response.status_code, 413)


if __name__ == "__main__":
    unittest.main()

