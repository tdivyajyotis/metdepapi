from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    database_url: str
    device_tokens: dict[str, str]
    read_api_token: str | None
    cors_origins: tuple[str, ...]
    max_payload_bytes: int

    @classmethod
    def from_env(cls) -> "Settings":
        raw_tokens = os.getenv("DEVICE_TOKENS", "{}")
        try:
            tokens = json.loads(raw_tokens)
        except json.JSONDecodeError as exc:
            raise RuntimeError("DEVICE_TOKENS must be a JSON object") from exc
        if not isinstance(tokens, dict) or not all(
            isinstance(key, str)
            and re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", key)
            and isinstance(value, str)
            and len(value) >= 32
            for key, value in tokens.items()
        ):
            raise RuntimeError(
                "DEVICE_TOKENS must map valid device IDs to tokens of at least 32 characters"
            )

        origins = tuple(
            item.strip()
            for item in os.getenv("CORS_ORIGINS", "").split(",")
            if item.strip()
        )
        return cls(
            database_url=os.getenv(
                "DATABASE_URL",
                "postgresql://sensor:sensor@postgres:5432/sensors",
            ),
            device_tokens=tokens,
            read_api_token=os.getenv("READ_API_TOKEN") or None,
            cors_origins=origins,
            max_payload_bytes=max(
                1024, min(int(os.getenv("MAX_PAYLOAD_BYTES", "65536")), 1048576)
            ),
        )

