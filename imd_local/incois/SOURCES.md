# INCOIS sources, operation and limitations

Verified live on 5 October 2026. Fifteen local products cover the accessible
forecast, advisory, image, observation and registry sources established during
this implementation. They use incois: storage names and publisher=INCOIS.
They are source-native extensions; the documented IMD schema remains separate.

## Implemented products

| Product | Public source and output | Live verification |
| --- | --- | --- |
| lsf-wave | Combined WW3 THREDDS: default HS, T02; optional advertised layers | Height, mean period, direction and wind components returned |
| lsf-current | RSMC HYCOM THREDDS: SSH, UVEL, VVEL | Surface level and current components returned |
| osf-sst | OSF page-advertised SST_NIO dataset | SST returned, about 29.87 C in the initial check |
| osf-mld | OSF page-advertised MLD_NIO: MLD, D20 | MLD returned; D20 was NaN and remains null/partial |
| osf-swell | Combined WW3: PHS01, PTP01 | Swell height and period returned |
| wave-alerts | SARAT mobile hwassalatestdata | Explicitly empty HWAJson and 68 SSAJson advisories |
| current-alerts | SAMUDRA mobile currentslatestdata | 15 advisories; wrapper dates retained separately |
| pfz | Public GeoServer PFZ_Automation WFS | 90 geographic features |
| abis | HAB products page and linked ABIS PNGs | 14 images downloaded and content-verified |
| locations | LSF flcLocations array | 1,263 entries; repeated source IDs preserved and disclosed |
| hf-radar-stations | OON fetchHFRadarBuoyData.jsp | 10 stations |
| hf-radar | OON fetchHFRadarVectors_2025.jsp | 5,046 vectors; 4,046 stale in the audit |
| tide-stations | Public Insitu_TideGauges_Tsunami WFS | 36 station features |
| tide-gauge | TEWS TideStations.xml and station chart JSON | Gopalpur Not Reporting, explicitly empty series |
| waman | Public wrb_data_stock.jsp charts | hm0, t1 and dirp; observations can be old |

## Source chain

The [LSF interface](https://incois.gov.in/oceanservices/LSF/index.html) publishes
wave/ocean catalogs and point-query logic. The
[OSF interface](https://incois.gov.in/oceanservices/osfforecast.jsp) advertises
current SST and MLD filenames. Sources are re-read for each collection.

- Catalogs: https://incois.gov.in/thredds/catalog/osf/ww3/catalog.xml
  and /thredds/catalog/osf/currents2/catalog.xml.
- Layers/time axes: /thredds/wms/<dataset> GetCapabilities.
- Units/fill values: /thredds/dodsC/<dataset>.das.
- Numerical subsets: /thredds/ncss/grid/<dataset>, one variable,
  latitude/longitude, explicit time window and accept=csv.
- Optional WMS sampling: GetFeatureInfo, CRS:84, explicit layer/time,
  text/plain. Returned coordinates identify the clicked point.
- Advisories: https://sarat.incois.gov.in/incoismobileappdata/rest/incois/hwassalatestdata
  and https://samudra.incois.gov.in/incoismobileappdata/rest/incois/currentslatestdata.
- PFZ/tide station features: public /geoserver/<workspace>/ows WFS.
  Exact URLs are retained in snapshots and the product registry.
- ABIS: [HAB products](https://incois.gov.in/site/services/hab_products.jsp),
  linked /datasets/ecosystem/HAB/Products/ABIS/ PNGs.
- Radar: /OON/fetchHFRadarBuoyData.jsp and fetchHFRadarVectors_2025.jsp;
  cm/s speed units are established by /OON/js2/legendHFRadar.js.
- Buoy: [public stock chart](https://incois.gov.in/site/datainfo/wrb_data_stock.jsp),
  buoy=<station>&parameter=hm0|t1|dirp.
- Tide: https://tsunami.incois.gov.in/itews/homexmls/TideStations.xml and
  /itews/JSONS/<PUBLISHED_STATION_NAME_UPPERCASE>_<days>.json, established
  by /TEWS/js/TGChartNat.js. Station names must match the registry.

Transport uses verified TLS, bounded responses/retries and no authentication
workaround. Gzip expansion and document redirects are bounded. Artifact
redirects must stay on HTTPS within the allowed publisher domain.
Some TEWS certificates need system trust: install the system-trust project
extra where default Python trust cannot validate them. Do not disable TLS.

## Running and serving

```text
python -m imd_local incois lsf-wave --param lat=19.24 --param lon=84.94 --param sampling=ncss
python -m imd_local incois osf-mld --param lat=19.24 --param lon=84.94 --param sampling=ncss
python -m imd_local incois wave-alerts
python -m imd_local incois waman --param station=Gopalpur --param parameter=hm0 --param limit=96
python -m imd_local incois tide-gauge --param station=Gopalpur --param days=1
python -m imd_local batch jobs.incois.json --watch
python -m imd_local serve
```

Forecast parameters: required finite lat/lon; optional layers (up to eight
advertised names), steps (1..56 per layer, default four), timezone-qualified
start, sampling=wms|ncss (CLI default wms), max_distance_km for NCSS (default
20, maximum 100). Jobs use NCSS with four future times per layer. Out-of-range
cells retain null values and a reason. The sample point 19.24 N, 84.94 E is
not asserted to be a measured buoy location. Scheduling intervals are local
choices, not publisher release guarantees. There are 18 example jobs.

Native parameters: locations accepts optional query; ABIS download=true|false;
WAMAN station/parameter/limit=1..512; tide-gauge exact station/days=1|7|30.
Others accept no parameters. WAMAN is a public visualization series, not a
registered bulk-download API. Its HTML may contain years of observations even
when only the latest 96 are selected.

| Local route | Meaning |
| --- | --- |
| /incois/coverage | Registered products and source selection |
| /incois/<product>?<parameters> | Usable stored payload |
| /incois/data/<product>?<parameters> | Full envelope, including stale data |
| /marine?lat=19.24&lon=84.94&city_id=43049 | Inspection of independent stored IMD/INCOIS components |

Exact parameters must match collection, including sampling=ncss for scheduled
forecasts. HTTP requests never fetch upstream. Missing/stale/expired payloads
return 503 and remain inspectable through the envelope route. The marine view
retains component provenance/freshness and does not assert city/point co-location.

## Docker operation

From the repository root, build the image and create the shared data volume:

```text
docker build -t imd-local .
docker volume create imd-data
docker run -d --name incois-collector --restart unless-stopped -v imd-data:/app/data imd-local batch jobs.incois.json --watch
docker run -d --name imd-api --restart unless-stopped -p 127.0.0.1:8000:8000 -v imd-data:/app/data imd-local serve --host 0.0.0.0
```

For an individual collection using the same persistent storage:

```text
docker run --rm -v imd-data:/app/data imd-local incois lsf-wave --param lat=19.24 --param lon=84.94 --param sampling=ncss
```

Read the [wave envelope](http://localhost:8000/incois/data/lsf-wave?lat=19.24&lon=84.94&sampling=ncss)
and [marine view](http://localhost:8000/marine?lat=19.24&lon=84.94&city_id=43049).
The marine view labels uncollected IMD components missing. Start the separate
IMD collector in [the Docker guide](../../DOCKER.md) to populate them.
That guide also covers logs, one-time batches, custom schedules, volume
persistence, updates and certificate configuration. Reuse existing named
containers instead of creating duplicates. Docker image execution remains
unverified in this workspace.

## Scientific meaning and source gaps

Native values and raw responses are retained. Forecast rows record valid time,
units, unit basis, fill/error status and normalized fields where established.
NCSS supplies actual returned cell coordinates and calculated distance per row.
WMS supplies clicked coordinates; interpolation remains unknown. Filename dates
and catalog modifications are not issue timestamps: issue_time remains null.
Forecast expiry uses available sampled valid times.

Verified normalized fields include hs_m, tm02_s, sst_c, mld_m, d20_m, ssh_m,
swell_height_m and swell_period_s. DIR DAS metadata says radians despite the
interface's degree label; conversion to wave_dir_deg is explicit, with
from/toward convention unknown. PWP units remain unknown. Wind/current speed
is derived only from verified m/s components at matching returned cells.
No bearing, vertical datum or unverified period definition is invented.

Buoy charts explicitly use UTC; hm0's cm unit converts to hs_m. QC is unknown.
Initial Gopalpur hm0/t1 samples were from September 2024 and remain stale when
retrieved today. Radar timestamps lack an explicit timezone; source day bounds
support conservative freshness checks. Mixed radar payloads containing stale
records are refused by the usable-payload route and remain inspectable.

Tide series retain separate publisher names (sensor, Predicted, Residual).
Chart values are labelled metres by the official script; datum/QC are unknown.
The chart encodes dates unusually, including negative axis numbers, so axis
values stay native and are not converted to Unix time. Gopalpur's empty series
is not a zero water level. Model SSH is never substituted for measured sea
level or tide predictions.

PFZ retains geometry and Year/Julian_day source dates. Advisory messages and
publisher labels stay native; validity without explicit timezone is not
silently converted. ABIS remains imagery without pixel-derived bloom values.
Filename dates are not issue times. Its 3 October images exceeded the default
48-hour source-age bound during this audit and remain inspectable as stale.

Registered holdings, bulk historical downloads, unverified satellite layers,
QC/datum definitions and exact release times need additional evidence or
authorized access. They are outside the completed accessible-source scope.
Docker execution remains unverified because Docker is absent; cross-platform
CI testing remains deferred as requested.

Validation: 61 tests pass. Stored API checks returned forecast payloads,
preserved stale envelopes and refused Gopalpur's empty tide payload with 503.
The marine view displayed stored, stale and unavailable components separately.

Root robots.txt returned 404, which grants no license. The official
[disclaimer](https://incois.gov.in/site/disclaimer.jsp) requires permission for
commercial reproduction. Publisher content is excluded from the project's
MIT license; see [DATA_LICENSE.md](../../DATA_LICENSE.md).
