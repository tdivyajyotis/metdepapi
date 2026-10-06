from __future__ import annotations

import json
from contextlib import contextmanager
from datetime import datetime
from typing import Any

import psycopg
from psycopg.rows import dict_row

from .measurements import flatten_numeric
from .security import token_digest
from .schemas import ReadingIn


SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS devices (
    device_id varchar(64) PRIMARY KEY,
    token_sha256 char(64) NOT NULL,
    enabled boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    last_seen_at timestamptz
);

CREATE TABLE IF NOT EXISTS sensor_readings (
    id bigserial PRIMARY KEY,
    event_id varchar(128) NOT NULL UNIQUE,
    device_id varchar(64) NOT NULL REFERENCES devices(device_id),
    sequence bigint NOT NULL,
    schema_version integer NOT NULL,
    observed_at timestamptz,
    received_at timestamptz NOT NULL DEFAULT now(),
    firmware varchar(64),
    uptime_ms bigint,
    wifi_rssi_dbm integer,
    payload jsonb NOT NULL
);

CREATE INDEX IF NOT EXISTS sensor_readings_device_time_idx
    ON sensor_readings (device_id, received_at DESC);
CREATE INDEX IF NOT EXISTS sensor_readings_observed_idx
    ON sensor_readings (observed_at DESC);

CREATE TABLE IF NOT EXISTS sensor_measurements (
    reading_id bigint NOT NULL REFERENCES sensor_readings(id) ON DELETE CASCADE,
    device_id varchar(64) NOT NULL,
    recorded_at timestamptz NOT NULL,
    metric text NOT NULL,
    value double precision NOT NULL,
    unit varchar(24),
    PRIMARY KEY (reading_id, metric)
);

CREATE INDEX IF NOT EXISTS sensor_measurements_series_idx
    ON sensor_measurements (device_id, metric, recorded_at DESC);
"""


class Database:
    def __init__(self, url: str):
        self.url = url

    @contextmanager
    def connect(self):
        with psycopg.connect(self.url, row_factory=dict_row) as connection:
            yield connection

    def initialize(self, device_tokens: dict[str, str]) -> None:
        with self.connect() as connection:
            for statement in SCHEMA_SQL.split(";"):
                if statement.strip():
                    connection.execute(statement)
            for device_id, token in device_tokens.items():
                connection.execute(
                    """
                    INSERT INTO devices (device_id, token_sha256)
                    VALUES (%s, %s)
                    ON CONFLICT (device_id) DO UPDATE
                    SET token_sha256 = EXCLUDED.token_sha256, enabled = true
                    """,
                    (device_id, token_digest(token)),
                )

    def authenticate_device(self, device_id: str, token: str) -> bool:
        with self.connect() as connection:
            row = connection.execute(
                """
                SELECT 1 FROM devices
                WHERE device_id = %s AND token_sha256 = %s AND enabled = true
                """,
                (device_id, token_digest(token)),
            ).fetchone()
            return row is not None

    def insert_reading(self, reading: ReadingIn) -> dict[str, Any]:
        payload = reading.model_dump(mode="json")
        measurements = list(flatten_numeric(reading.sensors))
        with self.connect() as connection:
            inserted = connection.execute(
                """
                INSERT INTO sensor_readings (
                    event_id, device_id, sequence, schema_version, observed_at,
                    firmware, uptime_ms, wifi_rssi_dbm, payload
                ) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s::jsonb)
                ON CONFLICT (event_id) DO NOTHING
                RETURNING id, received_at
                """,
                (
                    reading.event_id,
                    reading.device_id,
                    reading.sequence,
                    reading.schema_version,
                    reading.observed_at,
                    reading.firmware,
                    reading.uptime_ms,
                    reading.wifi_rssi_dbm,
                    json.dumps(payload),
                ),
            ).fetchone()
            duplicate = inserted is None
            if duplicate:
                inserted = connection.execute(
                    """
                    SELECT id, received_at FROM sensor_readings
                    WHERE event_id = %s AND device_id = %s
                    """,
                    (reading.event_id, reading.device_id),
                ).fetchone()
                if inserted is None:
                    raise ValueError("event_id is already owned by another device")
            else:
                recorded_at = reading.observed_at or inserted["received_at"]
                with connection.cursor() as cursor:
                    cursor.executemany(
                        """
                        INSERT INTO sensor_measurements
                            (reading_id, device_id, recorded_at, metric, value, unit)
                        VALUES (%s, %s, %s, %s, %s, %s)
                        """,
                        [
                            (
                                inserted["id"],
                                reading.device_id,
                                recorded_at,
                                metric,
                                value,
                                unit,
                            )
                            for metric, value, unit in measurements
                        ],
                    )
                connection.execute(
                    "UPDATE devices SET last_seen_at = now() WHERE device_id = %s",
                    (reading.device_id,),
                )
            return {
                "accepted": True,
                "duplicate": duplicate,
                "event_id": reading.event_id,
                "received_at": inserted["received_at"],
                "measurement_count": len(measurements),
            }

    def latest(self, device_id: str | None, limit: int) -> list[dict[str, Any]]:
        query = """
            SELECT event_id, device_id, sequence, observed_at, received_at,
                   firmware, uptime_ms, wifi_rssi_dbm, payload->'sensors' AS sensors
            FROM sensor_readings
        """
        params: list[Any] = []
        if device_id:
            query += " WHERE device_id = %s"
            params.append(device_id)
        query += " ORDER BY COALESCE(observed_at, received_at) DESC LIMIT %s"
        params.append(limit)
        with self.connect() as connection:
            return list(connection.execute(query, params).fetchall())

    def series(
        self,
        device_id: str,
        metric: str,
        since: datetime | None,
        limit: int,
    ) -> list[dict[str, Any]]:
        query = """
            SELECT recorded_at, value, unit
            FROM sensor_measurements
            WHERE device_id = %s AND metric = %s
        """
        params: list[Any] = [device_id, metric]
        if since:
            query += " AND recorded_at >= %s"
            params.append(since)
        query += " ORDER BY recorded_at DESC LIMIT %s"
        params.append(limit)
        with self.connect() as connection:
            rows = list(connection.execute(query, params).fetchall())
        rows.reverse()
        return rows

    def metrics(self, device_id: str) -> list[str]:
        with self.connect() as connection:
            rows = connection.execute(
                """
                SELECT DISTINCT metric FROM sensor_measurements
                WHERE device_id = %s ORDER BY metric
                """,
                (device_id,),
            ).fetchall()
            return [row["metric"] for row in rows]

    def devices(self) -> list[dict[str, Any]]:
        with self.connect() as connection:
            return list(
                connection.execute(
                    """
                    SELECT device_id, enabled, created_at, last_seen_at
                    FROM devices ORDER BY device_id
                    """
                ).fetchall()
            )

    def healthy(self) -> bool:
        with self.connect() as connection:
            return connection.execute("SELECT 1").fetchone() is not None
