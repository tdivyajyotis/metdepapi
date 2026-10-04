"""Portable batch jobs, freshness evaluation and offline parity reports."""
import json
import time
from datetime import datetime, timezone
from pathlib import Path

from .core import CollectionError, collect


def freshness(snapshot, max_age=86400, max_source_age=172800, now=None):
    now = now or datetime.now(timezone.utc)
    metadata = snapshot["metadata"]
    retrieved = datetime.fromisoformat(metadata["retrieved_at"])
    age = max(0, (now - retrieved).total_seconds())
    candidates = []
    for item in metadata.get("record_issue_times", []):
        # Prefer precise explicitly zoned issue time. Do not assume the timezone of SQL timestamps.
        updated = item.get("updated_at")
        if isinstance(updated, str) and (updated.endswith("Z") or "+" in updated[10:]):
            try:
                candidates.append(datetime.fromisoformat(updated.replace("Z", "+00:00")))
                continue
            except ValueError:
                pass
        date = item.get("date")
        if isinstance(date, str):
            try:
                # Date-only age is a conservative reporting-day bound, not a validity assertion.
                candidates.append(datetime.strptime(date[:10], "%Y-%m-%d").replace(tzinfo=timezone.utc))
            except ValueError:
                pass
    date = metadata.get("observation_date")
    if not candidates and isinstance(date, str):
        try:
            candidates.append(datetime.strptime(date, "%Y-%m-%d").replace(tzinfo=timezone.utc))
        except ValueError:
            pass
    source_ages = [max(0, (now - value).total_seconds()) for value in candidates]
    stale_records = sum(value > max_source_age for value in source_ages)
    return {"retrieval_age_seconds": age, "retrieval_stale": age > max_age,
            "source_time_known_records": len(candidates), "source_stale_records": stale_records,
            "oldest_source_age_seconds": max(source_ages) if source_ages else None,
            "source_time_status": "available_or_date_bound" if candidates else "unknown",
            "stale": age > max_age or stale_records > 0}


def load_jobs(path):
    try:
        config = json.loads(Path(path).read_text(encoding="utf-8"))
        jobs = config["jobs"]
        if not isinstance(jobs, list) or not jobs:
            raise ValueError()
        for job in jobs:
            if not isinstance(job, dict) or set(job) - {"product", "provider", "params", "interval_seconds"}:
                raise ValueError()
            if not isinstance(job.get("product"), str) or not isinstance(job.get("params", {}), dict):
                raise ValueError()
            if not all(isinstance(k, str) and isinstance(v, str) for k, v in job.get("params", {}).items()):
                raise ValueError()
            interval = job.get("interval_seconds", 3600)
            if isinstance(interval, bool) or not isinstance(interval, (int, float)) or not 60 <= interval <= 604800:
                raise ValueError()
        return jobs
    except (OSError, KeyError, TypeError, ValueError) as exc:
        raise CollectionError("configuration_error", "Jobs require product, string params and interval_seconds between 60 and 604800") from exc


def run_job(job, store, transport):
    product = job["product"]
    try:
        if product.startswith("artifact:"):
            if job.get("provider", "public") != "public":
                raise CollectionError("invalid_parameters", "Artifacts require the public provider")
            from .artifacts import collect_artifact
            result, raw = collect_artifact(product[9:], job.get("params", {}), transport,
                                           directory=store.path.parent / "artifacts")
        else:
            result, raw = collect(product, job.get("params", {}), job.get("provider", "public"), transport)
        store.save(result, raw)
        count = len(result.data) if isinstance(result.data,list) else (len(result.data["data"]) if isinstance(result.data,dict) and isinstance(result.data.get("data"),list) else 1)
        return {"product": product, "status": result.status, "records": count,
                "freshness": freshness(result.envelope())}
    except CollectionError as exc:
        # Failed runs never overwrite the last good snapshot.
        return {"product": product, "status": exc.status, "message": str(exc), "failed": True}


def run_jobs(jobs, store, transport, watch=False, emit=None):
    due = [0.0] * len(jobs)
    reports = []
    while True:
        for index, job in enumerate(jobs):
            if time.monotonic() < due[index]:
                continue
            report = run_job(job, store, transport)
            reports.append(report)
            if emit:
                emit(report)
            due[index] = time.monotonic() + job.get("interval_seconds", 3600)
        if not watch:
            return reports
        reports.clear()
        time.sleep(min(1, max(0.01, min(due) - time.monotonic())))


def compare_shapes(left, right, path="$", differences=None):
    differences = differences if differences is not None else []
    if type(left) is not type(right):
        differences.append({"path": path, "public_type": type(left).__name__, "official_type": type(right).__name__})
    elif isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            if key not in left or key not in right:
                differences.append({"path": path + "." + key, "missing_from": "public" if key not in left else "official"})
            else:
                compare_shapes(left[key], right[key], path + "." + key, differences)
    elif isinstance(left, list):
        # Compare all distinct record schemas without assuming equal ordering/count.
        def signatures(rows):
            return {json.dumps(schema(row), sort_keys=True) for row in rows}
        if signatures(left) != signatures(right):
            differences.append({"path": path + "[]", "public_schemas": sorted(signatures(left)),
                                "official_schemas": sorted(signatures(right))})
    return differences


def schema(value):
    if isinstance(value, dict):
        return {key: schema(item) for key, item in value.items()}
    if isinstance(value, list):
        return sorted({json.dumps(schema(item), sort_keys=True) for item in value})
    return type(value).__name__
