"""Public-page collectors. No JavaScript execution or platform dependencies."""
import html
import json
import re
from datetime import datetime
from urllib.parse import urlencode

from .core import CATALOG, CollectionError, Result

MAUSAM = "https://mausam.imd.gov.in/responsive/"
WFS = "https://reactjs.imd.gov.in/geoserver/imd/wfs"
LAYERS = {"districtwarning": "district_warnings_india", "districtnowcast": "NowcastWarningDistrict",
          "stationnowcast": "NowcastWarningStation"}
PERIODS = {"D": "Daily", "W": "Weekly", "M": "Monthly", "C": "Cumulative"}


def plain(text):
    return re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]*>", " ", text))).strip()


def rainfall_page(text, period):
    match = re.search(r'"areas"\s*:\s*(\[)', text)
    if not match:
        raise CollectionError("invalid_response", "Rainfall map data missing")
    try:
        areas, _ = json.JSONDecoder().raw_decode(text[match.start(1):])
    except ValueError as exc:
        raise CollectionError("invalid_response", "Rainfall map is not valid JSON") from exc
    labels = dict(re.findall(r'<label[^>]*for="(rb[5-8])"[^>]*>(.*?)</label>', text, re.S))
    label = plain(labels.get({"D": "rb5", "W": "rb6", "M": "rb7", "C": "rb8"}[period], ""))
    dates = re.findall(r"\d{2}-\d{2}-\d{4}", label)
    if not dates:
        raise CollectionError("invalid_response", "Rainfall reporting period missing")
    records = {}
    for area in areas:
        if isinstance(area, dict) and str(area.get("title", "")).startswith(("REGION :", "COUNTRY :")) and area.get("id") is None:
            continue  # Aggregate region decorations are not state records.
        if not isinstance(area, dict) or not area.get("title"):
            raise CollectionError("invalid_response", "Rainfall identity missing")
        text = plain(area.get("balloonText", ""))
        values = {}
        for key in ("Actual", "Normal", "Departure"):
            value = re.search(rf"{key}\s*:\s*(-?\d+(?:\.\d+)?%?)", text)
            values[key] = value.group(1) if value else None
        identity = str(area["id"]) if area.get("id") is not None else area["title"].strip()
        records[identity] = {"name": area["title"].strip(), **values}
    if not records:
        raise CollectionError("invalid_response", "Rainfall map contains no records")
    return records, dates, label


def category(actual, normal, departure):
    # Category is explicitly derived; no rounding of underlying rainfall values.
    try:
        actual_value, normal_value = float(actual), float(normal)
        if actual_value < 0 or normal_value < 0:
            return None
        if actual_value == 0 and normal_value == 0:
            return None  # zero normal makes a departure classification ambiguous
        percent = float(departure.rstrip("%"))
    except (TypeError, ValueError, AttributeError):
        return None
    if percent == -100 and actual_value == 0:
        return "NR"
    if percent >= 60:
        return "LE"
    if percent >= 20:
        return "E"
    if percent >= -19:
        return "N"
    if percent >= -59:
        return "D"
    if percent >= -99:
        return "LD"
    return None


def collect_rainfall(product, params, transport):
    if set(params) - {"id"}:
        raise CollectionError("invalid_parameters", "Rainfall accepts only the documented id filter")
    district = product == "districtrainfall"
    page = "rainfallinformation.php" if district else "rainfallinformation_state.php"
    raw, periods = {}, {}
    for code in PERIODS:
        raw[code] = transport.text(MAUSAM + page + "?" + urlencode({"msg": code}))
        periods[code] = rainfall_page(raw[code], code)
    daily, daily_dates, _ = periods["D"]
    # Public pages are fetched separately; refuse a cross-day mixed snapshot.
    for code, (_, dates, label) in periods.items():
        daily_label_dates = re.findall(r"Daily\s*\((\d{2}-\d{2}-\d{4})\)", plain(raw[code]))
        if not daily_label_dates or daily_label_dates[0] != daily_dates[0]:
            raise CollectionError("inconsistent_snapshot", "Rainfall pages changed reporting day during collection")
    selected = list(daily)
    if "id" in params:
        term = str(params["id"]).casefold()
        # District official id is numeric; state docs use a name (e.g. jammu).
        selected = [key for key, row in daily.items() if (key == term if district else term in row["name"].casefold())]
        if len(selected) != 1:
            raise CollectionError("not_found", "Rainfall filter is absent or ambiguous")
    output, derived, missing = [], [], {}
    for identity in selected:
        row = dict.fromkeys(CATALOG[product]["documented_fields"])
        row["District" if district else "State"] = daily[identity]["name"]
        if district:
            row["OBJ_ID"] = identity
        row["Date"] = datetime.strptime(daily_dates[0], "%d-%m-%Y").strftime("%Y-%m-%d") if district else daily_dates[0]
        for code, prefix in PERIODS.items():
            entries, dates, label = periods[code]
            entry = entries.get(identity)
            if entry is None or entry["name"] != daily[identity]["name"]:
                raise CollectionError("inconsistent_snapshot", "Rainfall geography differs across periods")
            actual_key = "Monthly Acutual" if not district and code == "M" else prefix + " Actual"
            departure_key = "Cumulative Departue Per" if not district and code == "C" else prefix + " Departure Per"
            row[actual_key], row[prefix + " Normal"], row[departure_key] = entry["Actual"], entry["Normal"], entry["Departure"]
            row[prefix + " Category"] = category(entry["Actual"], entry["Normal"], entry["Departure"])
            if row[prefix + " Category"] is not None:
                derived.append(prefix + " Category")
            if code != "D":
                date_key = "Week Date" if code == "W" else prefix + " Date"
                # Retain explicit bounds; never manufacture a missing end date.
                row[date_key] = " To ".join(dates)
        missing[identity] = [key for key, value in row.items() if value is None]
        output.append(row)
    metadata = {"source_url": MAUSAM + page, "source_urls": [MAUSAM + page + "?msg=" + code for code in PERIODS],
                "record_issue_times": [{"date": datetime.strptime(daily_dates[0], "%d-%m-%Y").strftime("%Y-%m-%d")}],
                "reporting_period_labels": {code: value[2] for code, value in periods.items()},
                "missing_fields": missing, "derived_fields": sorted(set(derived)),
                "compatibility": "documented_field_names; categories_derived; official_parity_unverified"}
    return Result(product, "public", "partial" if any(missing.values()) else "mapped", output, metadata), raw


def warning_mapping(product):
    fields = CATALOG[product]["documented_fields"]
    if product == "districtwarning":
        return {key: key for key in fields}
    mapping = {key: key for key in fields}
    mapping.update({f"Cat{i}": f"cat{i}" for i in range(1, 20)})
    mapping.update({"Vupto": "vupto", "color": "Color"})
    if product == "districtnowcast":
        mapping["Station"] = "State_District"
    return mapping


def map_warnings(payload, product, params):
    if not isinstance(payload, dict) or payload.get("type") != "FeatureCollection" or not isinstance(payload.get("features"), list):
        raise CollectionError("invalid_response", "Expected warning FeatureCollection")
    features = payload["features"]
    matched = payload.get("numberMatched", payload.get("totalFeatures"))
    if isinstance(matched, int) and matched != len(features):
        raise CollectionError("incomplete_response", "Warning feed was truncated; refusing partial national coverage")
    if not features:
        raise CollectionError("unavailable", "Warning feed is empty; this does not mean no warning")
    mapping = warning_mapping(product)
    output, issues, missing, excluded = [], [], {}, []
    for feature in features:
        raw = feature.get("properties") if isinstance(feature, dict) else None
        if isinstance(raw, dict) and raw.get("Obj_id") == 0 and raw.get("Date") == "":
            excluded.append({"feature_id": feature.get("id"), "reason": "undated_placeholder"})
            continue
        if not isinstance(raw, dict) or not raw.get("Date"):
            raise CollectionError("invalid_response", "Warning identity/date missing")
        identity = raw.get("Station") if product == "stationnowcast" else raw.get("Obj_id")
        if identity is None:
            raise CollectionError("invalid_response", "Warning identity missing")
        if "id" in params and str(identity).casefold() != str(params["id"]).casefold():
            continue
        row = {key: raw.get(source) for key, source in mapping.items()}
        if not row.get("Station" if product != "districtwarning" else "District"):
            raise CollectionError("invalid_response", "Warning place name missing")
        missing[str(identity)] = [key for key, value in row.items() if value is None]
        issues.append({"identity": identity, "date": raw["Date"],
                       "updated_at": raw.get("updated_at", raw.get("update_time"))})
        output.append(row)
    if not output:
        raise CollectionError("not_found", "Warning filter not found")
    metadata = {"source_url": WFS, "source_layer": LAYERS[product], "record_issue_times": issues,
                "missing_fields": missing, "compatibility": "documented_fields; official_wire_types_unverified",
                "excluded_records": excluded,
                "notes": ["Source issue times are preserved; collection does not establish current validity.",
                          "Colour codes are product-specific and retained without conversion."]}
    return Result(product, "public", "partial" if any(missing.values()) or excluded else "mapped", output, metadata)


def collect_public(product, params, transport):
    if product in ("districtrainfall", "staterainfall"):
        return collect_rainfall(product, params, transport)
    if set(params) - {"id"}:
        raise CollectionError("invalid_parameters", "Warnings accept only the documented id filter")
    mapping = warning_mapping(product)
    properties = set(mapping.values()) | {"updated_at" if product != "districtnowcast" else "update_time"}
    if product != "stationnowcast":
        properties.add("Obj_id")
    url = WFS + "?" + urlencode({"service": "WFS", "version": "1.1.0", "request": "GetFeature",
                                "typeName": "imd:" + LAYERS[product], "outputFormat": "application/json",
                                "propertyName": ",".join(sorted(properties))})
    payload = transport.json(url)
    result = map_warnings(payload, product, params)
    result.metadata["source_urls"] = [url]
    return result, payload
