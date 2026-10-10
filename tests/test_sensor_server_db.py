import importlib.util
import json
import unittest
from contextlib import contextmanager
from datetime import UTC, datetime, timedelta


SERVER_DEPS_AVAILABLE = bool(
    importlib.util.find_spec("pydantic") and importlib.util.find_spec("psycopg")
)

if SERVER_DEPS_AVAILABLE:
    from sensor_server.db import Database, trusted_observed_at
    from sensor_server.schemas import ReadingIn


class FakeResult:
    def __init__(self, row=None, rows=None, rowcount=0):
        self.row = row
        self.rows = rows or []
        self.rowcount = rowcount

    def fetchone(self):
        return self.row

    def fetchall(self):
        return self.rows


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
        self.calls = []

    def execute(self, query, params=None):
        self.execute_count += 1
        self.calls.append((query, params))
        if "RETURNING id, received_at" in query:
            return FakeResult(
                {"id": 7, "received_at": datetime(2026, 10, 7, tzinfo=UTC)}
            )
        if "count(*) AS reading_count" in query:
            return FakeResult(
                {
                    "reading_count": 2,
                    "oldest_received_at": datetime(2026, 10, 6, tzinfo=UTC),
                    "newest_received_at": datetime(2026, 10, 7, tzinfo=UTC),
                }
            )
        if "DELETE FROM sensor_readings" in query:
            return FakeResult(rowcount=2)
        return FakeResult()

    def cursor(self):
        return self.cursor_instance


@unittest.skipUnless(SERVER_DEPS_AVAILABLE, "install the 'server' extra")
class DatabaseTests(unittest.TestCase):
    def test_device_time_is_trusted_only_near_server_time(self):
        reference = datetime(2026, 10, 9, 0, 15, tzinfo=UTC)
        self.assertEqual(
            trusted_observed_at(reference - timedelta(minutes=4), reference),
            reference - timedelta(minutes=4),
        )
        self.assertIsNone(
            trusted_observed_at(reference + timedelta(minutes=6), reference)
        )

    def test_offline_replay_trusts_bounded_past_device_time(self):
        reference = datetime(2026, 10, 9, 0, 15, tzinfo=UTC)
        replayed = reference - timedelta(hours=24)
        self.assertEqual(
            trusted_observed_at(replayed, reference, stored_offline=True),
            replayed,
        )
        self.assertIsNone(
            trusted_observed_at(
                reference - timedelta(hours=37), reference, stored_offline=True
            )
        )
        self.assertIsNone(
            trusted_observed_at(
                reference + timedelta(minutes=6), reference, stored_offline=True
            )
        )

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
            telemetry={"version": 1, "uno_link": {"successes": 1}},
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
        insert_params = next(
            params for query, params in connection.calls if "INSERT INTO sensor_readings" in query
        )
        stored_payload = json.loads(insert_params[8])
        self.assertEqual(stored_payload["telemetry"]["uno_link"]["successes"], 1)

    def test_latest_orders_by_server_receipt_time(self):
        connection = FakeConnection()
        database = Database("unused")

        @contextmanager
        def fake_connect():
            yield connection

        database.connect = fake_connect
        database.latest("station-001", 25)

        query, params = connection.calls[-1]
        self.assertIn("ORDER BY received_at DESC, id DESC", query)
        self.assertEqual(params, ["station-001", 25])

    def test_purge_is_dry_run_until_confirmed(self):
        connection = FakeConnection()
        database = Database("unused")

        @contextmanager
        def fake_connect():
            yield connection

        database.connect = fake_connect
        cutoff = datetime(2026, 10, 9, 0, 12, tzinfo=UTC)
        dry_run = database.purge_readings_before("station-001", cutoff)
        self.assertEqual(dry_run["matched"], 2)
        self.assertEqual(dry_run["deleted"], 0)
        self.assertFalse(any("DELETE FROM sensor_readings" in query for query, _ in connection.calls))

        connection.calls.clear()
        confirmed = database.purge_readings_before(
            "station-001", cutoff, confirm=True
        )
        self.assertEqual(confirmed["deleted"], 2)
        self.assertTrue(any("DELETE FROM sensor_readings" in query for query, _ in connection.calls))


if __name__ == "__main__":
    unittest.main()
