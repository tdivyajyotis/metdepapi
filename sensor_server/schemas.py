from __future__ import annotations

from datetime import datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field, field_validator


class ReadingIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    schema_version: int = Field(default=1, ge=1, le=100)
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_.-]+$")
    event_id: str = Field(min_length=1, max_length=128)
    sequence: int = Field(ge=0)
    observed_at: datetime | None = None
    firmware: str | None = Field(default=None, max_length=64)
    uptime_ms: int | None = Field(default=None, ge=0)
    wifi_rssi_dbm: int | None = Field(default=None, ge=-150, le=20)
    sensors: dict[str, Any] = Field(default_factory=dict)
    telemetry: dict[str, Any] = Field(default_factory=dict)

    @field_validator("observed_at")
    @classmethod
    def require_timezone(cls, value: datetime | None) -> datetime | None:
        if value is not None and value.tzinfo is None:
            raise ValueError("observed_at must include a timezone")
        return value


class IngestResponse(BaseModel):
    accepted: bool
    duplicate: bool
    event_id: str
    received_at: datetime
    measurement_count: int


class ReadingBatchIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    readings: list[ReadingIn] = Field(min_length=1, max_length=8)


class BatchIngestResponse(BaseModel):
    accepted: bool
    reading_count: int
    inserted_count: int
    duplicate_count: int
    results: list[IngestResponse]
