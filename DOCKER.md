# Running IMD and INCOIS with Docker

Run these commands from the repository root with Docker installed and running.
The image uses Linux containers; select Linux container mode on Windows.
The commands below work in PowerShell and POSIX shells without shell-specific
line continuation. Docker image execution has not been verified in this
workspace because Docker is not installed. Cross-platform CI remains deferred.

## Build and persistent storage

```text
docker build -t imd-local .
docker volume create imd-data
```

The image includes the application, optional system certificate trust and PDF
text extraction, and all three schedules: jobs.example.json, jobs.full.json
(IMD), and jobs.incois.json (18 INCOIS jobs).
Its entrypoint is `python -m imd_local`; arguments after `imd-local` are
application arguments. The default command prints the IMD coverage catalog.

The volume is mounted at `/app/data`, containing `imd.sqlite`, downloaded
artifacts and caches. Use the same volume for every collector and the API.
Existing checkout data are excluded from the image and are not automatically
copied into this new volume.

## Start both collectors and the API

```text
docker run -d --name imd-collector --restart unless-stopped -v imd-data:/app/data imd-local batch jobs.full.json --watch
docker run -d --name incois-collector --restart unless-stopped -v imd-data:/app/data imd-local batch jobs.incois.json --watch
docker run -d --name imd-api --restart unless-stopped -p 127.0.0.1:8000:8000 -v imd-data:/app/data imd-local serve --host 0.0.0.0
```

The two collectors independently populate shared SQLite snapshots. The API
serves stored data and never fetches upstream on a request. Use
`jobs.example.json` instead of `jobs.full.json` for a smaller IMD schedule.
For INCOIS-only operation, start `incois-collector` and `imd-api`; the combined
view will label uncollected IMD components missing.

The service listens on all interfaces inside the container so Docker can
forward the port. The host mapping exposes it only on localhost.

## Check data and inspect limitations

Open these URLs after starting the containers:

| URL | Purpose |
| --- | --- |
| [Health](http://localhost:8000/health) | Server availability; does not establish upstream data availability |
| [IMD coverage](http://localhost:8000/coverage) | IMD documented fields and adapter coverage |
| [INCOIS coverage](http://localhost:8000/incois/coverage) | Fifteen INCOIS product definitions |
| [City forecast](http://localhost:8000/api/v1/cityforecast?id=43049) | Stored IMD example using documented keys |
| [Wave forecast](http://localhost:8000/incois/lsf-wave?lat=19.24&lon=84.94&sampling=ncss) | Default wave job's stored payload |
| [Wave envelope](http://localhost:8000/incois/data/lsf-wave?lat=19.24&lon=84.94&sampling=ncss) | Provenance, actual NCSS cells and freshness |
| [Buoy envelope](http://localhost:8000/incois/data/waman) | Default hm0 observations, including stale samples |
| [Tide envelope](http://localhost:8000/incois/data/tide-gauge) | Default Gopalpur station and source availability |
| [Marine view](http://localhost:8000/marine?lat=19.24&lon=84.94&city_id=43049) | Independent stored IMD and INCOIS components |

Collections start asynchronously. Payload routes can return 503 until an exact
parameter match exists. Explicit collection parameters must also appear in
retrieval URLs: scheduled forecasts use `sampling=ncss`. Custom layers/steps
create separate snapshots. The default wave job is separate from the additional
wave job with HS,T02,DIR,UWND,VWND.

An available server can still return 503 for stale, expired or unavailable
data. Inspection routes retain stale data. Gopalpur tide currently has no
series; older buoy/radar data and ABIS imagery can be stale. D20 can be null in
an otherwise partial MLD forecast. See the [INCOIS source guide](imd_local/incois/SOURCES.md).
Downloaded files are served at `/files/<sha256>.<extension>` using the file
names recorded in envelopes.

```text
docker logs --tail 100 imd-collector
docker logs --tail 100 incois-collector
docker logs --tail 100 imd-api
docker logs -f incois-collector
```

Ctrl+C stops following logs while the detached container continues running.
Watch mode reports each job and continues independent jobs after a failure.

## One-time collection

These commands write to the same persistent volume and exit after completion:

```text
docker run --rm -v imd-data:/app/data imd-local batch jobs.example.json
docker run --rm -v imd-data:/app/data imd-local batch jobs.incois.json
docker run --rm -v imd-data:/app/data imd-local collect cityforecast --param id=43049
docker run --rm -v imd-data:/app/data imd-local incois lsf-wave --param lat=19.24 --param lon=84.94 --param sampling=ncss
docker run --rm -v imd-data:/app/data imd-local incois tide-gauge --param station=Gopalpur --param days=1
```

A one-time batch exits nonzero if any job fails, while keeping independently
successful snapshots. Avoid launching duplicate collections of the same
schedule while its watch container is running.

For a custom schedule, replace the placeholder with the absolute path to an
existing JSON file. Quote the complete mount argument for paths with spaces:

```text
docker run --rm --mount "type=bind,source=<absolute-path-to-jobs.json>,target=/app/jobs.custom.json,readonly" -v imd-data:/app/data imd-local batch jobs.custom.json
```

To use a custom database, place the global option before the subcommand:

```text
docker run --rm -v imd-data:/app/data imd-local --database data/custom.sqlite incois wave-alerts
```

Collectors and the API must all use that database path to share its snapshots.

## Stop, restart and update

```text
docker stop imd-collector incois-collector imd-api
docker start imd-collector incois-collector imd-api
```

Container names must be unique. If these names already exist, start the existing
containers, or replace them to pick up a rebuilt image. After editing code or
the bundled schedules:

```text
docker build -t imd-local .
docker stop imd-collector incois-collector imd-api
docker rm imd-collector incois-collector imd-api
```

Then repeat the three start commands above. Include only names you actually
created. Removing containers preserves the named volume; do not remove
`imd-data` if you want to retain snapshots and files. A rebuilt image does not
change an already-created container.

For a consistent backup, stop both collectors before copying the whole data
directory, including any SQLite journal files:

```text
docker stop imd-collector incois-collector imd-api
docker cp imd-api:/app/data ./imd-data-backup
docker start imd-collector incois-collector imd-api
```

## Certificate configuration

TLS verification stays enabled. The image installs system-trust, which uses
the container's certificate store. Host-only organization certificates are
not automatically copied into it. If your network requires a supplied PEM
bundle, mount it and point IMD_CA_BUNDLE to the container path:

```text
docker run --rm --mount "type=bind,source=<absolute-path-to-ca-bundle.pem>,target=/app/ca-bundle.pem,readonly" -e IMD_CA_BUNDLE=/app/ca-bundle.pem -v imd-data:/app/data imd-local incois wave-alerts
```

Use the same mount/environment configuration for affected collector containers.
Do not put credentials or private certificate files into the build context.
The [data license](DATA_LICENSE.md) still applies to source content collected
inside Docker.
