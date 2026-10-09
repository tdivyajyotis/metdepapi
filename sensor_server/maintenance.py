from __future__ import annotations

import argparse
import json
import os
from datetime import datetime

from .db import Database


def aware_datetime(value: str) -> datetime:
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise argparse.ArgumentTypeError("timestamp must include a UTC offset")
    return parsed


def main() -> None:
    parser = argparse.ArgumentParser(description="Sensor database maintenance")
    subparsers = parser.add_subparsers(dest="command", required=True)
    purge = subparsers.add_parser(
        "purge-readings", description="Delete readings older than a receipt-time cutoff"
    )
    purge.add_argument("--device-id", required=True)
    purge.add_argument("--before", required=True, type=aware_datetime)
    purge.add_argument(
        "--confirm",
        action="store_true",
        help="perform the deletion; omission is a read-only dry run",
    )
    args = parser.parse_args()

    database_url = os.environ.get("DATABASE_URL")
    if not database_url:
        parser.error("DATABASE_URL is not set")

    database = Database(database_url)
    if args.command == "purge-readings":
        result = database.purge_readings_before(
            args.device_id, args.before, confirm=args.confirm
        )
        print(json.dumps(result, default=str, indent=2))


if __name__ == "__main__":
    main()
