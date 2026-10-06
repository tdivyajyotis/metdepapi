# IMD reference coverage and additional local capabilities

This document separates two things that can otherwise look alike:

1. **IMD-schema adapters** return records using field names documented in the
   [official IMD API reference](https://api.imd.gov.in/public/api_reference.html).
2. **Local artifact collectors** retrieve other public IMD pages, documents,
   images, GIS layers, and source-native datasets. Their outputs are explicitly
   not presented as undocumented official API responses.

The distinction matters. The IMD reference index currently lists 28 product
families, but it documents a callable path and enough response fields for only
part of them. A public page or a PDF can be useful input without establishing a
JSON contract that this project may safely emulate.

This guide describes the project as implemented on 5 October 2026. The IMD
reference and public sources can change independently of this repository.

## Official-reference adapters

The table below maps the project products to the corresponding categories in
the official reference. `collect` stores a source snapshot in SQLite; `serve`
then exposes a fresh stored snapshot under `/api/v1/<product>`.

| IMD category | Local product(s) | Public-source result |
| --- | --- | --- |
| City Weather Forecast | `cityforecast`, `cityforecastloc` | City observation and seven forecast slots. `cityforecastloc` also returns the documented latitude/longitude fields. |
| Current Weather | `current_wx` | Synoptic station observations, using the documented field names. Wind speed remains `null` when the public feed does not establish the documented unit. |
| District/Station Nowcast | `districtnowcast`, `stationnowcast` | Warnings and their documented category, message, validity, and colour fields. |
| District/State Rainfall | `districtrainfall`, `staterainfall` | Daily, weekly, cumulative, and monthly source values. The documented spelling mistakes in the state fields are intentionally retained. |
| AWS/ARG Data | `aws_data` | AWS records. The documented `id` filter is a call sign and `sid` is an IMD state ID. State/district values obtained from public geographic boundaries are labelled as derived. |
| River Basin QPF | `basinqpf` | Basin forecast ranges for five days and AAP. |
| District/Subdivision Warnings | `districtwarning`, `subdivisionwarning` | Public GIS warning layers translated to the documented fields. |
| Subdivision/State-District Rainfall Forecast | `subdivision_rainfall_forecast`, `state_district_rainfall_forecast` | Seven-day subdivision and five-day district forecasts, including distribution/colour fields. |
| Port Warning | `portwarning` | Public port records, dates, warning text, and a publisher only where the linked source explicitly identifies one. |
| Sea/Coastal Bulletin | `seabulletin`, `coastalbulletin` | Structured entries parsed from published HTML bulletin tables. Fields the source does not publish, such as some layer IDs or observation dates, remain `null`. |
| Sun/Moon Rise/Set | `sunmoon` | The documented wrapper for an exact, published city-station coordinate. It does not calculate astronomy events or select a nearby station. |

Use the normal collector form:

```text
python -m imd_local collect cityforecast --param id=43049
python -m imd_local collect aws_data --param id=NDL
python -m imd_local collect sunmoon --param lat=19.27 --param lon=84.88
```

The `official` provider is a passthrough for approved IMD API access. It is not
required for public collection and does not guess authentication headers.

## Why some reference items are not adapters

The IMD index links to more product families than it fully specifies. The
following documented endpoint names cannot be safely reproduced as public
schema adapters at present:

| Reference item | Current handling | Reason |
| --- | --- | --- |
| `cityforecast_mapping` | `artifact:city-stations` | The reference names the mapping endpoint but gives no record schema. The artifact returns source-native station discovery data. |
| `aws_data_mapping` | `artifact:aws-stations` | The reference names the mapping endpoint but gives no record schema. |
| Cyclone Track | `artifact:cyclone-track` | The public GIS source failed during the audit, so the collector reports a failure instead of pretending the result means “no cyclone.” |
| Cyclone Wind Warning | `artifact:cyclone-wind` | The public linked GeoJSON was empty/invalid during the audit. |
| Cyclone Cone of Uncertainty | `artifact:cyclone-cone` | The public linked GeoJSON was empty/invalid during the audit. |

These limitations concern the public fallback sources. They do not claim that
the information never exists in IMD systems or that the official, authenticated
API endpoint cannot provide it.

## Source-native artifact collectors

Artifact collectors expand the project beyond the documented response schemas.
Run one with `python -m imd_local artifact <name>`. By default, linked PDFs and
images are downloaded to content-addressed files and PDFs receive bounded text
extraction. Use `--index-only` when link discovery is sufficient.

All artifact results are saved under product names prefixed with `artifact:` and
are available from `/artifacts/<name>`. They carry a compatibility marker:

```text
source_native_only; not_an_official_API_response
```

### Documents, published bulletins, and images

| Artifact name | Public material collected | Result notes |
| --- | --- | --- |
| `national` | All India Weather Forecast Bulletin page and its current documents | Preserves original documents; text extraction preserves PDF page boundaries. |
| `marine` | Regional marine forecast/warning documents | Collects the regional documents exposed on the public page. |
| `coastal` | Coastal forecast page documents | Documents remain source-native; this is distinct from the structured `coastalbulletin` adapter. |
| `port` | RSMC port-warning page and linked PDFs | Includes source port arrays and documents; the structured `portwarning` adapter is the separate schema-compatible representation. |
| `sea` | RSMC sea-area bulletin PDFs | Complements the structured `seabulletin` adapter. |
| `coastal-bulletins` | RSMC coastal weather bulletin PDFs | Complements the structured `coastalbulletin` adapter. |
| `fishermen` | Fishermen-warning PDFs | No undocumented JSON warning schema is invented. |
| `highway` | Highway forecast/nowcast published documents | The API index distinguishes nowcast and five-day warning, but does not provide a response contract to split these into official-looking APIs. |
| `agromet` | National and state Agromet advisory documents exposed through the English state selector | Captures available PDFs and images and records per-state/document failures. |
| `radar` | Radar page images | Images are preserved as images; the collector does not infer measurements from pixels. |
| `district-forecast` | Public five-day state/district rainfall page and embedded map records where supplied | Useful source-native companion to `state_district_rainfall_forecast`; not an additional official endpoint. |

An image-only PDF remains an image-only document. No OCR text is manufactured.
Individual failed downloads are retained in metadata while successful documents
continue to be saved.

### Station discovery and public native data

| Artifact name | Parameters | What it collects |
| --- | --- | --- |
| `city-stations` | Optional `query` | The public city station search/registry. With no query it requests the full registry. |
| `aws-stations` | None | AWS point identifiers, call signs, names, and public geometry. |
| `mausamgram` | Required `lat`, `lon` | Website-native forecast payload at the source-selected 0.125-degree grid cell and currently published model cycle. |
| `lightning` | None | All five public lightning CSV windows independently. Successful CSVs retain their rows and source times; unavailable files are listed as failures. If no CSV is usable, the public animation may be retained as an image, explicitly not as point detections. |
| `radar-status` | None | Native public GIS station-status records. |
| `subdivision-warning` | None | Raw public GIS feature collection behind the structured subdivision-warning adapter. |
| `subdivision-rainfall` | None | Raw public GIS feature collection behind the structured subdivision-rainfall adapter. |
| `cyclone-track` | None | Raw public GIS layer when available; currently fails upstream rather than returning an empty success. |
| `cyclone-wind` | None | The four public wind GeoJSON products (27, 34, 50, and 64 knots) when valid. |
| `cyclone-cone` | None | The public cone GeoJSON when valid. |

Examples:

```text
python -m imd_local artifact national --index-only
python -m imd_local artifact lightning
python -m imd_local artifact mausamgram --param lat=19.27 --param lon=84.88
python -m imd_local artifact city-stations --param query=Delhi
```

## Local storage, freshness, and API service

The program is a collector first. It does not fetch IMD on every API request.
Each successful collection is saved to the configured SQLite database
(`data/imd.sqlite` by default), separated by product, provider, and exact
parameters. Raw source payloads are retained with the snapshot.

Start the stored-snapshot service:

```text
python -m imd_local serve
```

| Route | Purpose |
| --- | --- |
| `/health` | Confirms that the local server is running. It does not test IMD connectivity. |
| `/coverage` | Returns the IMD-reference catalog, including documented fields and implementation status. |
| `/api/v1/<product>` | Returns the body of a fresh stored schema-adapter snapshot. It returns `503` if no matching fresh snapshot exists and `501` for public products that lack an adapter. |
| `/data/<product>` | Returns a schema-adapter snapshot with the provenance/freshness envelope, including stale data for inspection. |
| `/artifacts/<name>` | Returns a stored source-native artifact envelope. Artifact snapshots remain inspectable when stale. |
| `/files/<sha256>.<extension>` | Serves a downloaded, content-addressed PDF/image file. |

The default API freshness policy rejects data retrieved more than 24 hours ago
and data with a known source timestamp over 48 hours old. Tune it for the
product and use case:

```text
python -m imd_local serve --max-age 3600 --max-source-age 7200
```

Fresh retrieval does not prove the underlying observation or bulletin is
current. The envelope keeps retrieval time, source times when available,
missing fields, source failures, and stale status separate.

## Automation and validation utilities

`jobs.example.json` contains a small starter IMD schedule. `jobs.full.json` covers
every IMD schema adapter plus every accessible non-redundant IMD artifact family
with representative parameters. `jobs.incois.json` adds 18 INCOIS jobs.
Batch mode continues other jobs if one source fails,
and a failed run never overwrites the last successful snapshot.

```text
python -m imd_local batch jobs.example.json
python -m imd_local batch jobs.full.json --watch
python -m imd_local batch jobs.incois.json --watch
```

Other safeguards and utilities:

| Command | Capability |
| --- | --- |
| `python -m imd_local search <text>` | Queries the public city station search endpoint. |
| `python -m imd_local validate <product> <json-file>` | Checks a payload’s keys, wrapper, and illustrated types against the saved IMD reference schema. It is not a value-equivalence test. |
| `python -m imd_local compare <public-json> <official-json>` | Compares outer shape, keys, and types across distinct record schemas. It does not prove units, IDs, validity, or meaning are equivalent. |
| `python -m imd_local coverage` | Prints the machine-readable reference/capability catalog. |

HTTP requests use certificate verification, bounded retry behavior for transient
network/429/5xx errors, and no retry for authentication failures. City collection
isolates individual station failures so one bad station cannot discard the rest
of a full registry run. Geographic lookups are cached to reduce repeated public
GIS requests, and ambiguous state/district boundaries remain `null`.

## Docker deployment

The supplied image runs the same portable Python program. Separate IMD and
INCOIS collectors and the API share a persistent Docker volume:

```text
docker build -t imd-local .
docker volume create imd-data
docker run -d --name imd-collector --restart unless-stopped -v imd-data:/app/data imd-local batch jobs.full.json --watch
docker run -d --name incois-collector --restart unless-stopped -v imd-data:/app/data imd-local batch jobs.incois.json --watch
docker run -d --name imd-api --restart unless-stopped -p 127.0.0.1:8000:8000 -v imd-data:/app/data imd-local serve --host 0.0.0.0
```

This exposes the service only on the host machine. The named volume keeps
snapshots, cached geographic data, and downloaded artifacts after the containers
are recreated. Collection parameters must match retrieval URLs, including
`sampling=ncss` for scheduled INCOIS forecasts. The `/marine` route preserves
missing/stale/unavailable component status. Docker is not installed in this
workspace, so image execution remains unverified. Refer to [DOCKER.md](DOCKER.md)
for full run, inspection, lifecycle, custom schedule and certificate steps,
or [README.md](README.md) for the quick-start.

## INCOIS extension

Fifteen INCOIS products cover wave/current/SST/MLD/swell forecasts,
high-wave/swell-surge and current advisories, PFZ geometry, ABIS images,
public buoy/radar/tide chart series and station/location registries. NCSS
subsets retain actual cell coordinates and distance; verified-unit conversions
retain source values. `/marine` provides stored inspection with independent
IMD/INCOIS provenance and freshness. The collectors use public INCOIS
THREDDS catalog, WMS capabilities, and numerical point service. They have a
separate `incois:` product namespace and `/incois/` stored-snapshot routes;
they do not modify any IMD contract. See the
[INCOIS source and usage guide](imd_local/incois/SOURCES.md) for verified
variables, parameters, sampling/units limitations, scheduling and current scope.

## Source, licensing, and scope

The official reference supplies field names and illustrated response structure;
it is not a blanket license for IMD data or documents. The project’s original
code and documentation use [MIT](LICENSE). IMD content, source-derived fixtures,
public documents, images, and third-party material retain their applicable
source rights and terms. See [DATA_LICENSE.md](DATA_LICENSE.md) before
redistributing collected material.

An artifact being public and collectable does not make it an official API,
confirm that it is complete, or authorize redistribution. The project records
those boundaries in its envelopes instead of filling missing values, inventing
schema fields, or treating an inaccessible source as a successful empty result.
