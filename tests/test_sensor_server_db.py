import importlib.util
import unittest
from contextlib import contextmanager
from datetime import UTC, datetime


SERVER_DEPS_AVAILABLE = bool(
    importlib.util.find_spec("pydantic") and importlib.util.find_spec("psycopg")
)

if SERVER_DEPS_AVAILABLE:
    from sensor_server.db import Database
    from sensor_server.schemas import ReadingIn


class FakeResult:
    def __init__(self, row=None):
        self.row = row

    def fetchone(self):
        return self.row


class FakeCursor:
    def __init__(self):
        self.calls = []

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def executemany(self, query, rows):
        self.calls.append((query, list(rows)))


class FakeConnection:
    def __init__(self):
        self.cursor_instance = FakeCursor()
        self.execute_count = 0

    def execute(self, query, params=None):
        self.execute_count += 1
        if "RETURNING id, received_at" in query:
            return FakeResult(
                {"id": 7, "received_at": datetime(2026, 10, 7, tzinfo=UTC)}
            )
        return FakeResult()

    def cursor(self):
        return self.cursor_instance


@unittest.skipUnless(SERVER_DEPS_AVAILABLE, "install the 'server' extra")
class DatabaseTests(unittest.TestCase):
    def test_insert_uses_cursor_for_measurement_batch(self):
        connection = FakeConnection()
        database = Database("unused")

        @contextmanager
        def fake_connect():
            yield connection

        database.connect = fake_connect
        reading = ReadingIn(
            device_id="station-001",
            event_id="event-1",
            sequence=1,
            observed_at=datetime(2026, 10, 7, tzinfo=UTC),
            sensors={
                "arduino_adc": {"raw_counts": 512, "ok": True},
                "tsl2584_1": {"visible_counts": 123},
            },
        )

        result = database.insert_reading(reading)

        self.assertFalse(result["duplicate"])
        self.assertEqual(result["measurement_count"], 2)
        self.assertEqual(len(connection.cursor_instance.calls), 1)
        rows = connection.cursor_instance.calls[0][1]
        self.assertEqual(rows[0][3:], ("arduino_adc.raw_counts", 512.0, "count"))
        self.assertEqual(
            rows[1][3:], ("tsl2584_sea.visible_counts", 123.0, "count")
        )


if __name__ == "__main__":
    unittest.main()
