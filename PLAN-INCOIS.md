# INCOIS extension delivery

Status: accessible-source implementation completed on 5 October 2026.
See [sources and operation](imd_local/incois/SOURCES.md) for verified requests,
live results, parameters and limitations.

## Delivered scope

| Plan area | Implementation |
| --- | --- |
| Discovery | Verified THREDDS, NCSS CSV, DAS units, advisories, GIS, images, charts and registries |
| Forecasts | Wave, surface currents/SSH, SST, MLD/D20 and swell point/time subsets |
| Observations | WAMAN chart series, HF-radar vectors and registry-validated tide series |
| Advisories/artifacts | High-wave/swell-surge/current messages, PFZ geometry and ABIS images |
| Normalization | Verified units and matching-cell vector speeds, native values retained |
| Metadata | Publisher, source URLs/raw data, time meaning, NCSS cell distance and null reasons |
| Storage/service | CLI, SQLite namespace, payload/envelope routes and product registry |
| Scheduling | 18 example jobs, bounded requests and failed-job snapshot preservation |
| Marine view | Independent stored IMD/INCOIS provenance and freshness |
| Validation | 61 passing tests covering parser/time/fill/distance, stale sources, storage, routes and transport; live collection |
| Packaging/docs | Portable core, optional system trust, Docker jobs, full run guide and source/content license docs |

INCOIS has its own source-native contract. IMD documented fields, catalog,
schema adapters and official passthrough remain separate. Normalized fields
are a local contract, not an official IMD or INCOIS API schema.

## Source limitations retained

- D20 is advertised but returned NaN at the selected point.
- Gopalpur tide gauge says Not Reporting with an empty series.
- Buoy and mixed radar records can be stale. Fetching does not reset their age.
- Repeated LSF IDs retain name/coordinates; they are not silently deduplicated.
- Issue times, some unit/direction conventions, tide axis encoding, datum and
  QC remain unknown rather than guessed.
- ABIS remains imagery; no bloom values are inferred from pixels.

## External and optional follow-ups

Registered/request-only holdings and bulk history require authorized access
and verified responses. Unverified satellite layers remain discovery leads.
A history table and scientific binary readers are optional future features;
the verified text subsets and snapshots do not require them.

Docker commands for both collectors, the API and persistent storage are in
[DOCKER.md](DOCKER.md). Image execution awaits an installed runtime.
Cross-platform CI testing remains
deferred under the user's instruction. Local Python collection and serving
have been implemented and checked.
