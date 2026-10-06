# Available-source coverage — 5 October 2026

The requested public-source implementation is complete for the sources that supplied usable data during this audit. **17 documented endpoint adapters** and **21 artifact/source collectors** are implemented. Schemas are taken from the saved [IMD reference](https://api.imd.gov.in/public/api_reference.html) only. Sample strings stay strings, nested sample wrappers are retained where applicable, and unavailable source fields stay null with sidecar quality metadata. No approved official responses or cross-platform CI run are required for this scope.

This report covers IMD. The separate [INCOIS source guide](imd_local/incois/SOURCES.md)
documents fifteen additional source-native products. [DOCKER.md](DOCKER.md)
provides run steps for both collectors and their shared stored-snapshot API.

## Documented endpoint coverage

| Product | Implemented public collection | Source limitations |
| --- | --- | --- |
| cityforecast, cityforecastloc | Single ID or complete public registry; city observations and seven slots | Source forecast dates kept separate; station failures recorded individually; national aggregate wrapper is unspecified by the reference |
| current_wx | 466 captured synoptic records; ID filter | Wind speed units unverified, so that field is null; observation dates formatted as documented |
| aws_data | 2,071 source points; documented call-sign `id` and state-ID `sid` filters | State/district assigned from public boundaries and labelled derived; overlaps remain null; feels-like unavailable; some records stale |
| districtrainfall, staterainfall | All four reporting periods and documented spelling inconsistencies | Categories derived and labelled; null where inputs are missing |
| districtwarning, districtnowcast, stationnowcast | Public WFS adapters and documented filters | Undated placeholders excluded; national issue dates may be mixed/stale |
| basinqpf | 220 basins, five day ranges and AAP | 88 records lack public object ID; 43 final captured records exceeded the default source-age limit |
| subdivisionwarning | All 36 subdivisions; exact documented fields and string warning descriptions | Unknown codes become null; warning colours expressed as hex strings |
| subdivision_rainfall_forecast | All 36 subdivisions, seven days, colour/distribution/percentage fields | Available public feed dated 15 September 2026; correctly marked stale |
| state_district_rainfall_forecast | All 751 captured districts, all five days and state names; zero missing fields in final capture | Each source balloon date is a forecast validity date; kept separately from retrieval/observation semantics |
| portwarning | 120 ports with decoded public IDs, dates and warnings; explicit publishers extracted from PDFs | 89 captured records lack an explicit publisher in their linked PDF and retain null; no inferred publisher |
| seabulletin | 16 sea areas parsed from live HTML tables | Layer IDs and observation dates not published in those tables; both null; validity UTC and issue time IST |
| coastalbulletin | 16 coastal areas parsed from live HTML tables | Layer IDs/observation dates unavailable; some TTT warning fields absent and remain null |
| sunmoon | Documented wrapper, four station-issued event times, exact published coordinates; Gopalpur verified | Arbitrary coordinates unsupported; no nearest-station interpolation or local calculation; source event-validity date unknown |

## Five endpoints unavailable or undocumented

| Product | Reason it has no public schema adapter |
| --- | --- |
| cityforecast_mapping | Reference names the endpoint but supplies no record schema. Complete public registry is available as `artifact:city-stations`. |
| aws_data_mapping | Reference names the endpoint but supplies no record schema. Public ID/call-sign/name/coordinates are available as `artifact:aws-stations`. |
| cyclone_track | Public WFS returns an upstream failure; other map references do not supply a usable current track. |
| cyclone_wind | Public linked wind GeoJSON files return empty content. Native query interface reports failure, not no warning. |
| cyclone_cou | Public linked cone GeoJSON returns empty content. Native query interface reports failure, not no cyclone. |

Their official adapters still exist. No undocumented fields, endpoints, station IDs or empty-success weather records are manufactured.

## Document and index-only products

- National bulletin: current PDF downloaded and text extracted; 19 linked page images retained as duplicate renderings.
- Marine/fishermen warnings: five regional warning PDFs extracted; dedicated fishermen collection downloaded/extracted all 15 linked PDFs.
- Sea-area bulletins: both RSMC PDFs downloaded/extracted, alongside the structured HTML adapter.
- Coastal bulletins: 15 linked PDFs downloaded; 13 yielded text and two are image-only.
- Port bulletins: all seven linked PDFs downloaded and text extracted; used for explicit publisher enrichment.
- Highway: six regional documents downloaded; three PDF files yielded text and the others are images/image-only PDFs. No guessed separation into undocumented NHAI API schemas.
- Radar: seven images downloaded; 39 public station statuses captured separately. Source status dates may be stale.
- Agromet: all states exposed by the public English selector traversed; 49 document links discovered, 24 downloaded, including 22 state images and two readable PDFs. The other 25 published PDF links return empty/unavailable content, including through HTTPS. Failures are recorded per URL.
- Mausamgram: model cycle and website grid selection retained; 15 native series with 41 entries captured at the Gopalpur grid point.
- Lightning: three linked five-minute CSV bins are accessible; the records are dated February 2024 and explicitly stale. The full/current and combined 15-minute feeds are unavailable. Public animation fallback is implemented if the CSV feeds are unavailable; it cannot create point detections.
- Station discovery: complete city registry and AWS point registry retained in their public schemas because the mapping endpoints' schemas are undocumented.

Index-only products have no schema or endpoint path in the reference. They remain native artifact responses at `/artifacts/<name>` rather than invented IMD APIs. Downloaded files are served by content hash at `/files/<hash>.<extension>`; PDF text preserves page boundaries and image-only documents remain available as original files.

## Verification and runtime

**41 local tests pass**, covering documented keys/types/wrappers, unknown/missing values, all five forecast dates, identity joins, exact-coordinate astronomy, feed truncation, stale refusals, bounded retries, partial job failures, provider/parameter isolation, verified document formats, content hashes and geographic overlap handling. New mapped products and the document families above were checked live. Complete national city fan-out is tested with fixtures; its thousands of network requests were not run during this audit.

`jobs.full.json` runs every implemented mapped product and all non-redundant accessible artifact families with representative city/AWS parameters. Remove those filters for national collection. National city fan-out and the first full AWS district lookup can take substantial time; geographic lookups are cached for 30 days. A fresh fetch does not make old source records current. API routes refuse age-policy violations; inspection routes retain snapshots and source-age information.

Cross-platform CI testing is deferred as requested. Optional historical-grid integration and calculated astronomy are outside the documented-schema scope of this delivery. Official response comparison remains optional future validation; it is not a pending implementation requirement here.
