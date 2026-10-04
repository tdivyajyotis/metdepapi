# IMD local

Local IMD collection with replaceable public and official providers. Seventeen public products use documented API field names: city forecasts and coordinates, district/state rainfall, district warnings, district/station nowcasts, synoptic observations, AWS observations, basin QPF, subdivision warnings, subdivision rainfall forecasts, five-day district rainfall forecasts, port warnings, sea bulletins, coastal bulletins and station-issued sun/moon times. Public documents and additional forecast datasets have a separate artifact interface. Includes city search, official JSON passthrough, SQLite snapshots, bounded retries, portable periodic collection and a local HTTP API. See [COVERAGE.md](COVERAGE.md) for limitations and remaining work.

See [PLAN.md](PLAN.md) for the full implementation roadmap and compatibility rules. Runs on Python 3.11+ on Windows, macOS and Linux, without platform shell commands. The core uses the standard library; optional `truststore` uses system certificate trust across platforms and `pypdf` extracts public PDF text. Not affiliated with IMD.

## Install (any supported OS)

Create a virtual environment with `python -m venv .venv`, activate it using your shell's standard command, then install:

```text
python -m pip install -e ".[system-trust,documents]"
```

Use `python3` instead of `python` if that is your system's Python command. The optional extra is recommended on managed networks. A standard-library-only installation is `python -m pip install -e .`.

## Run from this checkout

```powershell
python -m imd_local coverage
python -m imd_local search Gopalpur
python -m imd_local collect cityforecast --param id=43049
python -m imd_local collect cityforecastloc --param id=43049
python -m imd_local collect staterainfall --param id=odisha
python -m imd_local collect districtrainfall
python -m imd_local collect districtwarning
python -m imd_local collect stationnowcast --param id=Gopalpur
python -m imd_local collect current_wx
python -m imd_local collect aws_data
python -m imd_local collect basinqpf
python -m imd_local artifact mausamgram --param lat=19.27 --param lon=84.88
python -m imd_local artifact national
python -m imd_local artifact port
python -m imd_local batch jobs.example.json
python -m imd_local batch jobs.example.json --watch
python -m imd_local serve
```

Optional installation: `python -m pip install -e .` provides the `imd` command.

Rainfall collects daily, weekly, monthly and cumulative map data. Categories are derived using documented thresholds and labelled in metadata. State field spellings such as `Monthly Acutual` and `Cumulative Departue Per` match the reference. Reporting period bounds remain source-provided.

Warnings use the public map's WFS feeds without geographic geometry. Undated placeholders are excluded and listed in metadata; incomplete feeds are rejected. Collection does not establish current warning validity: inspect source issue dates and validity times. District IDs are IMD object IDs; station nowcast IDs are names, not city station numbers.

## Local API

- `http://127.0.0.1:8000/api/v1/cityforecast?id=43049`: stored data using documented field names.
- `http://127.0.0.1:8000/data/cityforecast?id=43049`: same data with metadata/quality status.
- `/coverage`: documented fields, official endpoint availability and implemented public providers.
- `/health`: server health, not upstream availability.

The server reads collected snapshots and never fetches on incoming requests. Missing or stale API snapshots return 503; unsupported public products return 501. `/data/<product>` retains stale snapshots with freshness metadata. Defaults reject retrieval age over 24 hours or any known record source age over 48 hours; configure `serve --max-age 86400 --max-source-age 172800` in seconds. Date-only source ages use a conservative UTC reporting-day bound. Unknown source times remain explicitly unknown; age checks do not establish forecast validity. Use tighter policies appropriate to your application, especially nowcasts.

`batch` continues independent jobs after a failure, reports each result and returns a failing exit code if any job fails. `--watch` runs each job at its configured interval and stops with Ctrl+C. It is a foreground Python process, portable across operating systems; run it under your deployment's process supervisor for unattended operation. Transient connection/429/5xx errors receive at most two retries; 401/403 are not retried. A failed run never replaces a good snapshot.

## Public artifacts

`artifact` supports national, marine, sea, coastal and fishermen bulletins; port PDFs; regional highway documents; national/state agromet advisories; radar images/status; Mausamgram; lightning time-bin CSVs; station discovery; and cyclone native-source queries. Run `python -m imd_local artifact --help` for the full list. Cyclone sources currently fail or return empty content rather than proving no cyclone.

By default, document collectors download public PDFs/images into content-addressed files beside the database and extract PDF text by page. Image-only PDFs remain available as files with `text_status=image_only`; no guessed OCR text is emitted. Download failures are recorded per URL and successful documents are retained. `--index-only` discovers links without downloading. The `documents` extra enables PDF extraction; without it files still download and extraction is labelled `requires_documents_extra`. No whole third-party scraper applications are bundled.

Read saved artifact envelopes at `/artifacts/<name>` with exactly the collected parameters. Downloaded files are available at `/files/<sha256>.<extension>`. Artifact responses remain inspectable when stale. Mausamgram uses the site's published model cycle and 0.125-degree grid selection. The lightning collector checks all five publicly linked CSV windows independently and reports missing windows. None of these index-only products has an invented official endpoint or schema.

Six new mapped commands:

```text
python -m imd_local collect subdivisionwarning
python -m imd_local collect subdivision_rainfall_forecast
python -m imd_local collect state_district_rainfall_forecast
python -m imd_local collect portwarning
python -m imd_local collect seabulletin
python -m imd_local collect coastalbulletin
```

The district forecast reads all five days and joins state identity by public IMD object ID; 751 captured records contain every documented field. Dates in each forecast slot advance with validity and are retained separately in metadata. Unknown codes stay null. Subdivision warning text and rainfall distribution strings are decoded from public map categories; output colours use the reference's canonical colour conventions rather than numeric WFS codes.

Marine HTML tables preserve bulletin content, validity and publisher. API layer IDs and observation dates are not present in those tables and remain null; validity is UTC and issue time is IST. Public port IDs are decoded from the map's `id_array`; the reference does not enumerate them. Publisher text is extracted from linked port PDFs when explicitly present. AWS state/district fields are assigned from public IMD boundaries, labelled derived, with ambiguous overlaps left null. The documented AWS `id` filter uses call signs (for example `NDL`), and `sid` uses the state IDs in the reference. Boundary lookups are cached for 30 days. Feels-like temperature and unverified synoptic wind units remain null.

`python -m imd_local validate PRODUCT payload.json` checks the saved reference's illustrated keys, nested shapes and types, permitting explicit missing nulls. Products with field tables use exact field-set checks. Products with no documented schema return `undocumented_schema`; the collector does not invent one.

## Official access switch

```powershell
# Set IMD_OFFICIAL_HEADERS_JSON privately, using exactly the headers IMD supplies.
python -m imd_local collect cityforecast --provider official --param id=43049
python -m imd_local serve --provider official
```

The same local route now serves the official response unchanged. IP approval, credentials and upstream API contract verification still need to be established. No guessed API-key header or automatic authentication bypass is provided. Public adapters take field names and illustrated types from the IMD reference only. Fields absent from public sources remain null, with missing-field metadata. Table-only products do not define every wire type or national aggregate wrapper; the local aggregate is an array. Official response comparison is optional and is not a completion gate for this docs-only implementation.

Export the product payloads from approved official responses and public collection, then run `python -m imd_local compare public.json official.json`. This checks outer shape, keys and types across all distinct record schemas; it returns exit code 1 on differences. It does not prove value, unit, ID or validity equivalence. Compare payloads rather than provenance envelopes. Credentials never belong in fixture files.

## TLS and tests

TLS verification remains enabled. Install the optional `system-trust` extra to use the OS certificate store through portable Python networking. Alternatively set `IMD_CA_BUNDLE` to an administrator-provided PEM bundle. No PowerShell or platform networking subprocess is used by the application.

```powershell
python scripts/build_catalog.py
python -m unittest discover -s tests -v
```

Tests use captured city, rainfall, warnings, district/subdivision forecasts and marine sources, plus PDF content verification. They verify documented keys, missing values, identity, temporal separation, category boundaries, official passthrough, feed completeness and cache isolation. Captures are under `references/` and `tests/fixtures/`; collected snapshots go to ignored `data/imd.sqlite`. GitHub Actions checks Python 3.11 and 3.13 on Windows, macOS and Linux. Only local Windows execution has been verified in this workspace so far.

## Complete available-source collection

```text
python -m imd_local batch jobs.full.json
python -m imd_local artifact city-stations
python -m imd_local collect cityforecast
python -m imd_local collect sunmoon --param lat=19.27 --param lon=84.88
python -m imd_local collect aws_data --param id=NDL
python -m imd_local collect aws_data --param sid=7
```

`jobs.full.json` includes every implemented schema adapter and all non-redundant accessible artifact families. It uses a representative city and AWS call sign for a practical first run; omit the city/AWS filters to collect the national registries. National city collection and the first full AWS geographic assignment can require thousands of sequential requests. Do that initial capture separately before running frequent warning jobs. Failed stations do not cancel the remaining city collection.

The public `sunmoon` adapter supports exact, unambiguous station coordinates only, checks the city source coordinates again, and never substitutes a nearby station or calculated events. It uses the documented status/message/totalCount/data wrapper. The city source does not state the event-validity date; metadata labels that unknown and keeps observation date separate.

Cross-platform CI execution is deferred as requested. Local fixture and live-source checks are the validation performed for this delivery.

For products whose reference illustrates a single record object (AWS and rainfall), an `id` filter yielding one record returns that documented object. National/state aggregate responses use arrays because the reference does not illustrate their aggregate wrapper. Validation checks each documented record shape in those arrays.
