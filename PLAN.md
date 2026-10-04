# Completed available-source plan

Scope: collect every accessible product found in the IMD reference and its linked public sites, maintain documented schemas only, and defer cross-platform CI testing. [COVERAGE.md](COVERAGE.md) is the final product/source audit.

## Completed

1. Reference catalogue with exact field names, documented examples and AWS state IDs; nested wrapper/type validation against the reference only.
2. Seventeen public schema adapters: cities, synoptic/AWS observations, rainfall, nowcasts/warnings, basin QPF, subdivision forecasts/warnings, five-day district forecasts, ports, sea/coastal bulletins and exact-station sun/moon times.
3. Twenty-one source/artifact interfaces: document families, radar, station registries, Mausamgram, lightning and native map queries. PDF/image downloads are hashed; PDF text is extracted by page, and empty/image-only/failed sources are labelled.
4. Single/national city collection; documented AWS call-sign and state queries; state/district geographic enrichment with overlaps left null and provenance separated from official records.
5. Portable batch scheduling, sequential request spacing, bounded transient retries, SQLite provenance, freshness refusals, inspection routes and safe content-hash file serving.
6. Local fixture and live-source validation, full collection configuration, usage documentation and final coverage report. Cross-platform CI execution is deferred.

## Schema rules

- Use the reference's exact endpoint paths, query names, field spellings, sample types and illustrated nested structures.
- Field tables do not specify every wire type or national aggregate wrapper. Keep this uncertainty explicit; do not invent an official wrapper for a table-only aggregate.
- Missing source fields are null with separate missing-field metadata. Unknown warning codes are not converted to no warning.
- Preserve observation, validity, publication and retrieval dates separately. Source age is never reset by collection.
- Index-only products and mapping endpoints without record schemas remain native artifacts. Do not create undocumented API fields.
- Official mode remains explicit passthrough; credentials never enter source snapshots or Git. Official response comparison is optional future verification, not required for this docs-only scope.

## Closed source gaps

Two mapping endpoints have no documented record schema. Three cyclone structured sources are failing/empty. Some agromet PDFs are empty, some PDF documents are image-only, lightning/subdivision rainfall are stale, and several mapped fields are absent from public sources. These are recorded source limitations, not unfinished parser work or permission requests. Collection preserves the available data and reports the unavailable parts.

Run `python -m imd_local batch jobs.full.json`, then `python -m imd_local serve`. Use `--watch` for periodic collection. Initial national city/AWS collection may require thousands of requests; seed it separately from frequent warning jobs.
