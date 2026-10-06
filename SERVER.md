# Sensor ingestion server

This service accepts the JSON produced by `firmware/nodemcu-sensor-node`, stores
the original payload in PostgreSQL, flattens every numeric sensor leaf into a
queryable time series, and serves a small dashboard at `/`.

The storage format is deliberately sensor-agnostic. An Arduino Uno can therefore
send ADC readings through the NodeMCU as, for example:

```json
{
  "arduino_adc": {
    "ok": true,
    "reference_v": 5.0,
    "resolution_bits": 10,
    "channels": [
      {"channel": 0, "raw_counts": 512, "voltage_v": 2.502},
      {"channel": 1, "raw_counts": 203, "voltage_v": 0.992}
    ]
  }
}
```

Those values become metrics such as
`arduino_adc.channels.0.raw_counts` without a schema migration.

## Start locally

1. Copy `.env.server.example` to `.env`.
2. Replace the database password and every device token with random values.
3. Start the API and database:

```powershell
docker compose -f compose.server.yaml up --build -d
```

4. Open <http://localhost:8000> or the API documentation at
   <http://localhost:8000/docs>.

The Compose port is bound to `127.0.0.1`, not the LAN or public internet.

## Test ingestion

```powershell
$headers = @{
  Authorization = "Bearer replace-with-a-long-random-device-token"
  "X-Device-ID" = "station-001"
}
$body = @{
  schema_version = 1
  device_id = "station-001"
  event_id = "station-001-test-1"
  sequence = 1
  observed_at = (Get-Date).ToUniversalTime().ToString("o")
  firmware = "test"
  sensors = @{
    arduino_adc = @{
      ok = $true
      reference_v = 5.0
      resolution_bits = 10
      channels = @(@{channel = 0; raw_counts = 512; voltage_v = 2.502})
    }
  }
} | ConvertTo-Json -Depth 8

Invoke-RestMethod -Method Post -Uri http://localhost:8000/v1/readings `
  -Headers $headers -ContentType application/json -Body $body
```

Repeating the same request is safe: the unique `event_id` returns a successful
response with `duplicate: true` rather than creating a second reading.

## API routes

- `POST /v1/readings` — device-authenticated ingestion.
- `GET /v1/devices` — configured devices and last-seen timestamps.
- `GET /v1/readings` — recent full sensor snapshots.
- `GET /v1/metrics?device_id=station-001` — discovered metric paths.
- `GET /v1/series?device_id=station-001&metric=...` — chart-ready series.
- `GET /healthz` — database health.
- `GET /docs` — interactive OpenAPI documentation.

If `READ_API_TOKEN` is set, the GET routes require that bearer token. The
included browser dashboard does not retain a read token, so either leave it
unset behind Cloudflare Access or put the dashboard behind a same-origin proxy
that supplies authentication.

## Cloudflare Tunnel

Create a tunnel in Cloudflare Zero Trust and route the public hostname to
`http://api:8000`. Put its token in `.env`, then start the optional profile:

```powershell
docker compose -f compose.server.yaml --profile tunnel up --build -d
```

Protect the dashboard/read API with Cloudflare Access. The NodeMCU ingestion
route must remain reachable by the device; it is separately protected by its
per-device bearer token. Do not publish the PostgreSQL container.

## Backups

The database is stored in the `sensor-db` Docker volume. A tunnel is not a
backup. Schedule `pg_dump` to storage outside that volume and test restoration.

