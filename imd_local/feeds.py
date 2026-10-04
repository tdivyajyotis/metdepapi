"""Verified public WFS products with conservative IMD field mappings."""
from urllib.parse import urlencode

from .core import CATALOG, CollectionError, Result
from .products import WFS

LAYERS = {"current_wx": "synop_data_layer", "aws_data": "aws_data_layer",
          "basinqpf": "indian_river_basin"}
QPF = {1: "0 mm", 2: "0.1 - 10 mm", 3: "11 - 25 mm", 4: "26 - 50 mm",
       5: "51 - 100 mm", 6: "> 100 mm"}
MAPPINGS = {
    "current_wx": {"Station Id": "station_id", "Station": "station",
                   "Date of Observation": "dat", "Time of Observation": "utc",
                   "M.S.L.P": "mslp", "Wind Direction": "winddir",
                   "Temperature": "dbtemp", "Weather Code": "weather",
                   "Nebulosity": "nebulosity", "Humidity": "rh", "Last 24 hrs Rainfall": "24hrlyrain"},
    "aws_data": {"ID": "id", "CALL_SIGN": "call_sign", "STATION": "station", "DATE": "dat",
                 "TIME": "time", "CURR_TEMP": "temp", "DEW_POINT_TEMP": "dewpoint", "RH": "rh",
                 "WIND_DIRECTION": "winddir", "WIND_SPEED": "windspeed", "MSLP": "mslp",
                 "MIN_TEMP": "temp_min", "MAX_TEMP": "temp_max", "WEATHER_CODE": "weather",
                 "NEBULOSITY": "nebulosity"},
    "basinqpf": {"Obj_Id": "obj_id", "Date": "issue_at", "FMO": "FMO", "Basin": "BASIN",
                 "SubBasin": "SUBBASIN", "Area (Sq. Km.)": "AREA_SQKM", "AAP": "aap"},
}


def features(payload):
    if not isinstance(payload, dict) or payload.get("type") != "FeatureCollection":
        raise CollectionError("invalid_response", "Expected public FeatureCollection")
    rows = payload.get("features")
    count = payload.get("numberMatched", payload.get("totalFeatures"))
    if not isinstance(rows, list) or not rows:
        raise CollectionError("unavailable", "Public feed contains no observations")
    if isinstance(count, int) and count != len(rows):
        raise CollectionError("incomplete_response", "Public feed was truncated")
    if not all(isinstance(row, dict) and isinstance(row.get("properties"), dict) for row in rows):
        raise CollectionError("invalid_response", "Invalid feature properties")
    return rows


def map_feed(product, payload, params):
    if set(params) - ({"id", "sid"} if product == "aws_data" else {"id"}):
        raise CollectionError("invalid_parameters", "This public feed accepts an id filter only")
    output, times, missing = [], [], {}
    identity_key = {"current_wx": "station_id", "aws_data": "id", "basinqpf": "obj_id"}[product]
    for feature in features(payload):
        raw = feature["properties"]
        identity = raw.get(identity_key)
        if identity is None and product != "basinqpf":
            raise CollectionError("invalid_response", "Feed identity missing")
        filter_identity = raw.get("call_sign") if product == "aws_data" else identity
        if "id" in params and str(filter_identity).casefold() != str(params["id"]).casefold():
            continue
        row = dict.fromkeys(CATALOG[product]["documented_fields"])
        row.update({key: raw.get(value) for key, value in MAPPINGS[product].items()})
        if product == "aws_data":
            geometry = feature.get("geometry") or {}
            coords = geometry.get("coordinates", [])
            if geometry.get("type") == "Point" and len(coords) >= 2:
                row["Longitude"], row["Latitude"] = coords[:2]
            # A 1970 time anchor encodes time of day, not observation date.
            if isinstance(row["TIME"], str) and row["TIME"].startswith("1970-01-01T"):
                row["TIME"] = row["TIME"].split("T")[1]
        if product == "basinqpf":
            for day in range(1, 6):
                row[f"Day{day}"] = QPF.get(raw.get(f"day{day}"))
        for key, value in list(row.items()):
            if value in ("", "NULL"):
                row[key] = None
        for key in ("Date of Observation", "DATE", "Date"):
            value = row.get(key)
            if isinstance(value, str) and len(value) == 11 and value.endswith("Z"):
                row[key] = value[:10]
        missing[str(identity) if identity is not None else feature.get("id", raw.get("SUBBASIN"))] = [key for key, value in row.items() if value is None]
        times.append({"identity": identity, "date": raw.get("dat", raw.get("issue_at")),
                      "time": raw.get("utc", raw.get("time")),
                      "updated_at": raw.get("update_time", raw.get("updated_at"))})
        output.append(row)
    if not output:
        raise CollectionError("not_found", "Public feed id not found")
    return Result(product, "public", "partial" if any(missing.values()) else "mapped", output,
                  {"source_url": WFS, "source_layer": LAYERS[product], "record_issue_times": times,
                   "missing_fields": missing, "compatibility": "documented_fields; official_parity_unverified",
                   "notes": ["Source date/time values are retained; wire types are provisional.",
                             "Synoptic Wind Speed is null pending verification of source units; AWS units are unverified."]
                   if product != "basinqpf" else ["Day ranges use the public QPF map legend; AAP retains the source millimetres."]})


def collect_feed(product, params, transport):
    query = {"service": "WFS", "version": "1.1.0", "request": "GetFeature",
             "typeName": "imd:" + LAYERS[product], "outputFormat": "application/json"}
    if product == "basinqpf":
        query["propertyName"] = ",".join(sorted(set(MAPPINGS[product].values()) | {f"day{day}" for day in range(1, 8)} | {"updated_at"}))
    url = WFS + "?" + urlencode(query)
    payload = transport.json(url)
    result = map_feed(product, payload, params)
    result.metadata["source_urls"] = [url]
    if product == "aws_data":
        from .geography import assign_states, load_states, state_matches
        if "sid" in params and str(params["sid"]) not in CATALOG[product].get("state_ids", {}):
            raise CollectionError("invalid_parameters", "AWS sid is not listed in the IMD reference")
        try:
            states, state_url = load_states(transport)
            assign_states(result.data, states)
            result.metadata["source_urls"].append(state_url)
            result.metadata["derived_fields"] = ["STATE"]
            result.metadata["state_assignment"] = "Public IMD polygon containment; boundary vintage and overlaps may leave nulls; not AWS-issued state metadata"
            if "sid" in params:
                wanted = CATALOG[product]["state_ids"][str(params["sid"])]
                result.data = [row for row in result.data if state_matches(row.get("STATE"), wanted)]
                if not result.data:
                    raise CollectionError("not_found", "No AWS records assigned to requested state")
        except CollectionError as exc:
            if "sid" in params: raise
            result.metadata["state_assignment_status"] = exc.status
        from .geography import assign_districts
        result.metadata["failed_district_lookups"] = assign_districts(result.data, transport)
        result.metadata.setdefault("derived_fields", []).append("DISTRICT")
        result.metadata["district_assignment"] = "Public IMD spatial point lookup; overlaps/border gaps remain null; not AWS-issued district metadata. Cached for 30 days."
        result.metadata["missing_fields"] = {str(row["ID"]): [key for key,value in row.items() if value is None] for row in result.data}
        selected_ids = {str(row["ID"]) for row in result.data}
        result.metadata["record_issue_times"] = [item for item in result.metadata["record_issue_times"] if str(item["identity"]) in selected_ids]
    return result, payload
