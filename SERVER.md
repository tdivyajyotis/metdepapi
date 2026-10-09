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
- `GET /v1/readings` — recent full sensor and one-way telemetry snapshots.
- `GET /v1/metrics?device_id=station-001` — discovered metric paths.
- `GET /v1/series?device_id=station-001&metric=...` — chart-ready series.

The directional light sensors use stable physical names throughout the API and
dashboard: I2C address `0x29` is `tsl2584_sea`, and `0x49` is
`tsl2584_land`. On the first API startup after this change, the database
migration renames the corresponding keys in historical JSON payloads and the
flattened time-series metrics. Ingestion also maps the former `tsl2584_1` and
`tsl2584_2` keys, allowing the server to be deployed before the NodeMCU is
flashed.

The dashboard refreshes every 30 seconds and uses server receipt time to decide
which reading is latest, so a damaged field clock cannot pin an old boot above
current data. It provides station-health summaries, grouped/filterable compact
graphs for every numeric sensor channel, a recent-arrivals table, and a
collapsible one-way telemetry console. There is deliberately no API or UI route
that sends commands to field hardware.

Device observation timestamps more than five minutes from the API server clock
remain preserved inside the original JSON payload, but the indexed observation
time falls back to server `received_at`. This keeps new time series ordered even
if an RTC is temporarily corrupt.

## Purging test readings

Database maintenance is available only inside the API container; it is not
exposed as a public HTTP endpoint. The command is a read-only dry run unless
`--confirm` is supplied. For the October 9 commissioning data, retain the clean
single-supply boot and inspect the exact matched count first:

```powershell
docker compose -f compose.server.yaml exec api python -m sensor_server.maintenance purge-readings --device-id station-001 --before "2026-10-09T05:42:30+05:30"
```

If the displayed cutoff and count are correct, perform the cascading deletion:

```powershell
docker compose -f compose.server.yaml exec api python -m sensor_server.maintenance purge-readings --device-id station-001 --before "2026-10-09T05:42:30+05:30" --confirm
```

Deleting a reading also deletes its flattened measurements through the database
foreign key. Readings received at or after the cutoff are retained.

- `GET /healthz` — database health.
- `GET /docs` — interactive OpenAPI documentation.

If `READ_API_TOKEN` is set, the GET routes require that bearer token. The
included browser dashboard does not retain a read token, so either leave it
unset behind Cloudflare Access or put the dashboard behind a same-origin proxy
that supplies authentication.

## Cloudflare Tunnel

The canonical public hostnames are:

| Purpose | Public route |
| --- | --- |
| IMD data/API | `https://imd.turtleguard.in` |
| Sensor dashboard/read API | `https://dashboard.turtleguard.in` |
| Device ingestion | `https://ingest.turtleguard.in/v1/readings` |

For the ingestion published-application route, keep the path expression
`^/v1/readings$` so the unprotected device hostname cannot expose dashboard or
read routes. Protect `dashboard.turtleguard.in` with Cloudflare Access; do not
put interactive Access authentication in front of the ingestion route.

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
