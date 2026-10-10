from __future__ import annotations

from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse

from .auth import bearer_token
from .config import Settings
from .db import Database
from .security import tokens_match
from .schemas import BatchIngestResponse, IngestResponse, ReadingBatchIn, ReadingIn


settings = Settings.from_env()
database = Database(settings.database_url)
static_dir = Path(__file__).with_name("static")


@asynccontextmanager
async def lifespan(_: FastAPI):
    database.initialize(settings.device_tokens)
    yield


app = FastAPI(
    title="Local Environmental Station API",
    version="0.1.0",
    lifespan=lifespan,
)

if settings.cors_origins:
    app.add_middleware(
        CORSMiddleware,
        allow_origins=list(settings.cors_origins),
        allow_credentials=False,
        allow_methods=["GET"],
        allow_headers=["Authorization", "Content-Type"],
    )


@app.middleware("http")
async def limit_payload_size(request: Request, call_next):
    raw_length = request.headers.get("content-length")
    if raw_length:
        try:
            if int(raw_length) > settings.max_payload_bytes:
                return JSONResponse(
                    status_code=status.HTTP_413_CONTENT_TOO_LARGE,
                    content={"detail": "Request body is too large"},
                )
        except ValueError:
            return JSONResponse(status_code=400, content={"detail": "Invalid Content-Length"})
    return await call_next(request)


def require_read_access(authorization: str | None = Header(default=None)) -> None:
    if settings.read_api_token is None:
        return
    if not tokens_match(bearer_token(authorization), settings.read_api_token):
        raise HTTPException(status_code=401, detail="Invalid bearer token")


@app.get("/", include_in_schema=False)
def dashboard() -> FileResponse:
    return FileResponse(static_dir / "index.html")


@app.get("/healthz")
def health() -> dict[str, str]:
    return {"status": "ok" if database.healthy() else "unhealthy"}


@app.post("/v1/readings", response_model=IngestResponse, status_code=201)
def ingest_reading(
    reading: ReadingIn,
    authorization: str | None = Header(default=None),
    x_device_id: str | None = Header(default=None),
):
    if x_device_id is not None and x_device_id != reading.device_id:
        raise HTTPException(status_code=400, detail="X-Device-ID does not match payload")
    token = bearer_token(authorization)
    if not database.authenticate_device(reading.device_id, token):
        raise HTTPException(status_code=401, detail="Unknown device or invalid token")
    try:
        result = database.insert_reading(reading)
    except ValueError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    return JSONResponse(status_code=200 if result["duplicate"] else 201, content=IngestResponse(**result).model_dump(mode="json"))


@app.post("/v1/readings/batch", response_model=BatchIngestResponse, status_code=201)
def ingest_reading_batch(
    batch: ReadingBatchIn,
    authorization: str | None = Header(default=None),
    x_device_id: str | None = Header(default=None),
):
    device_id = batch.readings[0].device_id
    if any(reading.device_id != device_id for reading in batch.readings):
        raise HTTPException(status_code=400, detail="A batch must contain one device_id")
    if x_device_id is not None and x_device_id != device_id:
        raise HTTPException(status_code=400, detail="X-Device-ID does not match payload")
    token = bearer_token(authorization)
    if not database.authenticate_device(device_id, token):
        raise HTTPException(status_code=401, detail="Unknown device or invalid token")

    results: list[IngestResponse] = []
    try:
        for reading in batch.readings:
            results.append(IngestResponse(**database.insert_reading(reading)))
    except ValueError as exc:
        # Inserts completed before a conflict are harmless on retry because
        # event_id is idempotent and duplicates return a successful result.
        raise HTTPException(status_code=409, detail=str(exc)) from exc

    duplicate_count = sum(result.duplicate for result in results)
    response = BatchIngestResponse(
        accepted=True,
        reading_count=len(results),
        inserted_count=len(results) - duplicate_count,
        duplicate_count=duplicate_count,
        results=results,
    )
    return JSONResponse(
        status_code=200 if duplicate_count == len(results) else 201,
        content=response.model_dump(mode="json"),
    )


@app.get("/v1/readings", dependencies=[Depends(require_read_access)])
def latest_readings(
    device_id: str | None = Query(default=None, max_length=64),
    limit: int = Query(default=50, ge=1, le=2000),
):
    return {"items": database.latest(device_id, limit)}


@app.get("/v1/devices", dependencies=[Depends(require_read_access)])
def devices():
    return {"items": database.devices()}


@app.get("/v1/metrics", dependencies=[Depends(require_read_access)])
def metrics(device_id: str = Query(min_length=1, max_length=64)):
    return {"device_id": device_id, "items": database.metrics(device_id)}


@app.get("/v1/series", dependencies=[Depends(require_read_access)])
def series(
    device_id: str = Query(min_length=1, max_length=64),
    metric: str = Query(min_length=1, max_length=200),
    since: datetime | None = None,
    limit: int = Query(default=500, ge=1, le=5000),
):
    return {
        "device_id": device_id,
        "metric": metric,
        "items": database.series(device_id, metric, since, limit),
    }
