"""Publisher-native advisories, public chart observations and registries."""
import html
import json
import math
import re
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone
from urllib.parse import urlencode, urljoin, urlsplit

from ..core import CollectionError, Result
from ..document_content import document_content

BASE = "https://incois.gov.in"
NATIVE = {
    "wave-alerts": {"kind": "advisory", "url": "https://sarat.incois.gov.in/incoismobileappdata/rest/incois/hwassalatestdata"},
    "current-alerts": {"kind": "advisory", "url": "https://samudra.incois.gov.in/incoismobileappdata/rest/incois/currentslatestdata"},
    "pfz": {"kind": "advisory", "url": BASE + "/geoserver/PFZ_Automation/ows?service=WFS&version=1.1.0&request=GetFeature&typeName=PFZ_Automation:pfzlines&outputFormat=application/json"},
    "abis": {"kind": "images", "url": BASE + "/site/services/hab_products.jsp"},
    "locations": {"kind": "registry", "url": BASE + "/oceanservices/LSF/index.html"},
    "hf-radar-stations": {"kind": "registry", "url": BASE + "/OON/fetchHFRadarBuoyData.jsp"},
    "hf-radar": {"kind": "observation", "url": BASE + "/OON/fetchHFRadarVectors_2025.jsp"},
    "tide-stations": {"kind": "registry", "url": BASE + "/geoserver/Insitu_TideGauges_Tsunami/ows?service=WFS&version=1.0.0&request=GetFeature&typeName=Insitu_TideGauges_Tsunami:Tideguages57&outputFormat=application/json"},
    "waman": {"kind": "observation", "url": BASE + "/site/datainfo/wrb_data_stock.jsp"},
    "tide-gauge": {"kind": "observation", "url": "https://tsunami.incois.gov.in/itews/homexmls/TideStations.xml"},
}


def feature_collection(value):
    if not isinstance(value, dict) or value.get("type") != "FeatureCollection" or not isinstance(value.get("features"), list):
        raise CollectionError("invalid_response", "Expected public INCOIS FeatureCollection")
    count = value.get("numberMatched", value.get("totalFeatures"))
    if isinstance(count, int) and count != len(value["features"]):
        raise CollectionError("incomplete_response", "INCOIS feature collection is truncated")
    if any(not isinstance(row, dict) or not isinstance(row.get("properties"), dict) for row in value["features"]):
        raise CollectionError("invalid_response", "Invalid INCOIS feature records")
    return value


def locations(text):
    match = re.search(r"const\s+flcLocations\s*=\s*(\[)", text)
    if not match:
        raise CollectionError("invalid_response", "Public LSF registry is missing")
    # Quote only the known object keys; never evaluate publisher JavaScript.
    encoded = re.sub(r"([{,]\s*)(id|name|lat|lon)\s*:", r'\1"\2":', text[match.start(1):])
    try:
        rows, _ = json.JSONDecoder().raw_decode(encoded)
        if not rows:
            raise ValueError()
        for row in rows:
            if not isinstance(row["name"], str) or not -90 <= float(row["lat"]) <= 90 or not -180 <= float(row["lon"]) <= 180:
                raise ValueError()
    except (ValueError, KeyError, TypeError) as exc:
        raise CollectionError("invalid_response", "Invalid LSF registry") from exc
    return rows


def chart_rows(text, station, parameter, limit):
    if not re.search(r'name=["\']buoy["\']\s+value=["\']' + re.escape(station) + r'["\']', text):
        raise CollectionError("invalid_response", "Buoy chart did not confirm the requested station")
    match = re.search(r"series\s*:\s*\[\s*\{.*?data\s*:\s*(\[\[)", text, re.S)
    if not match or not re.search(r"useUTC\s*:\s*true", text):
        raise CollectionError("unavailable", "Buoy chart has no explicit UTC numerical series")
    unit_match = re.search(r'yAxis\s*:.*?text\s*:\s*\["([^"]+)"\]', text, re.S)
    unit = unit_match[1] if unit_match else None
    try:
        points, _ = json.JSONDecoder().raw_decode(text[match.start(1):])
        rows = []
        for point in points:
            if not isinstance(point, list) or len(point) != 2 or not isinstance(point[0], (int, float)):
                raise ValueError()
            when = datetime.fromtimestamp(point[0] / 1000, timezone.utc).isoformat()
            value = point[1]
            if value is not None and (not isinstance(value, (int, float)) or not math.isfinite(value)):
                raise ValueError()
            rows.append({"station": station, "parameter": parameter, "observation_time": when,
                         "value": value, "units": unit, "qc": None,
                         "normalized": {"hs_m": value / 100} if value is not None and parameter == "hm0" and unit == "Cm" else {}})
        rows.sort(key=lambda row: row["observation_time"])
        if not rows:
            raise ValueError()
        return rows[-limit:], len(rows)
    except (ValueError, TypeError, OverflowError, OSError) as exc:
        raise CollectionError("invalid_response", "Invalid buoy chart series") from exc


def collect_native(name, params, transport, directory):
    spec = NATIVE[name]
    allowed = {"station", "parameter", "limit"} if name == "waman" else ({"station", "days"} if name == "tide-gauge" else ({"download"} if name == "abis" else ({"query"} if name == "locations" else set())))
    if set(params) - allowed:
        raise CollectionError("invalid_parameters", "Unexpected INCOIS native product parameters")
    raw, failures = {}, []
    url = spec["url"]
    metadata = {"kind": spec["kind"], "record_issue_times": []}
    if name == "tide-gauge":
        station, days = params.get("station", "Gopalpur"), params.get("days", "1")
        if days not in {"1", "7", "30"}:
            raise CollectionError("invalid_parameters", "Tide days must be 1, 7 or 30")
        raw[url] = transport.text(url)
        try:
            registry = ET.fromstring(raw[url])
            matches = [row for row in registry.findall("station") if row.findtext("statrealName") == station]
        except ET.ParseError as exc:
            raise CollectionError("invalid_response", "Invalid tide station registry") from exc
        if len(matches) != 1 or not re.fullmatch(r"[A-Za-z0-9 _-]{1,60}", station):
            raise CollectionError("invalid_parameters", "Select an exact published tide station name")
        entry = matches[0]
        metadata["station"] = {child.tag: child.text for child in entry if child.tag != "html"}
        metadata["station"]["status"] = entry.get("status")
        series_url = "https://tsunami.incois.gov.in/itews/JSONS/" + station.upper() + "_" + days + ".json"
        raw[series_url] = transport.json(series_url)
        data = raw[series_url]
        if not isinstance(data, list) or any(not isinstance(s, dict) or not isinstance(s.get("name"), str) or not isinstance(s.get("data"), list) for s in data):
            raise CollectionError("invalid_response", "Invalid tide chart series")
        for series in data:
            for point in series["data"]:
                if not isinstance(point, list) or len(point) != 2 or any(v is not None and (not isinstance(v, (int, float)) or not math.isfinite(v)) for v in point):
                    raise CollectionError("invalid_response", "Invalid tide chart point")
        metadata.update({"availability": "source_series" if data else "no_series", "units": "m",
                         "unit_basis": "public TGChartNat.js Water Level(m)", "datum": None, "qc_status": "not_published",
                         "chart_time_encoding": "publisher_native; not_converted_to_Unix_time",
                         "representation": "separate_named_sensor_predicted_and_residual_series; not_model_SSH"})
    elif name == "waman":
        station, parameter = params.get("station", "Gopalpur"), params.get("parameter", "hm0")
        try:
            limit = int(params.get("limit", "96"))
            if not 1 <= limit <= 512 or not re.fullmatch(r"[A-Za-z0-9 -]{1,60}", station) or parameter not in {"hm0", "dirp", "t1"}:
                raise ValueError()
        except ValueError as exc:
            raise CollectionError("invalid_parameters", "Use a station name, hm0/dirp/t1 and limit 1..512") from exc
        url += "?" + urlencode({"buoy": station, "parameter": parameter})
        raw[url] = transport.text(url)
        data, total = chart_rows(raw[url], station, parameter, limit)
        metadata.update({"chart_total_points": total, "representation": "public_visualization_series; not_download_API", "qc_status": "not_published"})
        metadata["record_issue_times"] = [{"updated_at": row["observation_time"]} for row in data]
    elif name == "abis":
        if params.get("download", "true") not in {"true", "false"}:
            raise CollectionError("invalid_parameters", "download must be true or false")
        raw[url] = transport.text(url)
        links = sorted({urljoin(url, html.unescape(v)) for v in re.findall(r'(?:src|href)=["\']([^"\']+)["\']', raw[url])
                        if urlsplit(urljoin(url, v)).path.startswith("/datasets/ecosystem/HAB/Products/ABIS/") and urlsplit(v).path.lower().endswith(".png")})
        if not links:
            raise CollectionError("unavailable", "No public ABIS product images linked")
        if len(links) > 32:
            raise CollectionError("incomplete_response", "Unexpected number of ABIS product images")
        data = {"documents": []}
        for link in links:
            item = {"url": link, "kind": "image", "content_verified": False}
            try:
                if params.get("download", "true") == "true":
                    item = document_content(item, transport, directory, allowed_domains=("incois.gov.in",))
            except CollectionError as exc:
                item["download_status"] = exc.status
                failures.append({"source_url": link, "status": exc.status})
            data["documents"].append(item)
        metadata["representation"] = "source_images; no_pixel_to_bloom_measurement_conversion"
        # A source filename is labelled as such; it is not assumed to be issue time.
        metadata["source_filename_date_labels"] = sorted(set(re.findall(r"([A-Za-z]{3}\d{4}-d\d{2})", " ".join(links))))
        for label in metadata["source_filename_date_labels"]:
            try:
                metadata["record_issue_times"].append({"date": datetime.strptime(label, "%b%Y-d%d").date().isoformat(), "basis": "source_filename_date_label; not_issue_time"})
            except ValueError:
                pass
    elif name == "locations":
        raw[url] = transport.text(url)
        data = locations(raw[url])
        metadata["duplicate_source_ids"] = sorted({r["id"] for r in data if sum(v["id"] == r["id"] for v in data) > 1})
        metadata["identity_note"] = "Source IDs are not unique; retain name and coordinates with ID"
        if params.get("query"):
            data = [row for row in data if params["query"].casefold() in row["name"].casefold()]
    else:
        raw[url] = transport.json(url)
        if name in {"pfz", "tide-stations"}:
            data = feature_collection(raw[url])
            if name == "pfz":
                for feature in data["features"]:
                    props = feature["properties"]
                    try:
                        year, day = int(props["Year"]), int(props["Julian_day"])
                        date = datetime(year, 1, 1) + timedelta(days=day-1)
                        if day < 1 or date.year != year:
                            raise ValueError()
                        metadata["record_issue_times"].append({"date": date.date().isoformat(), "basis": "source_Year_and_Julian_day"})
                    except (KeyError, TypeError, ValueError, OverflowError):
                        pass
        elif name.endswith("alerts"):
            fields = {"wave-alerts": ("HWAJson", "SSAJson"), "current-alerts": ("CurrentsJson",)}[name]
            if not isinstance(raw[url], dict) or any(f not in raw[url] for f in fields):
                raise CollectionError("invalid_response", "INCOIS advisory wrapper changed")
            data = {}
            for field in fields:
                try:
                    rows = json.loads(raw[url][field]) if isinstance(raw[url][field], str) else raw[url][field]
                    if not isinstance(rows, list) or any(not isinstance(r, dict) or "Message" not in r for r in rows):
                        raise ValueError()
                except (ValueError, TypeError) as exc:
                    raise CollectionError("invalid_response", "Invalid INCOIS advisory records") from exc
                data[field] = rows
                for row in rows:
                    try:
                        day = datetime.strptime(row["Issue Date"], "%d-%m-%Y").date().isoformat()
                        metadata["record_issue_times"].append({"date": day})
                    except (KeyError, ValueError, TypeError):
                        pass
            metadata["publisher_date_labels"] = {k: v for k, v in raw[url].items() if k not in fields}
            metadata["validity_status"] = "in_source_message; not_parsed_without_explicit_timezone"
        else:
            data = raw[url]
            if not isinstance(data, list) or not data or any(not isinstance(r, dict) for r in data):
                raise CollectionError("invalid_response", "Invalid INCOIS observation/registry list")
            if name == "hf-radar":
                for row in data:
                    if not {"observation_time", "lat", "lng", "speed", "direction"} <= set(row):
                        raise CollectionError("invalid_response", "HF radar vector fields changed")
                metadata["source_units"] = {"speed": "cm/s", "basis": "public OON legendHFRadar.js"}
                metadata["observation_timezone"] = None
                metadata["qc_status"] = "not_published"
                metadata["record_issue_times"] = [{"date": r["observation_time"][:10]} for r in data]
    metadata.update({"publisher": "INCOIS", "parameters": params, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                     "compatibility": "source_native_only; not_an_official_API_response", "source_urls": list(raw), "failed_sources": failures})
    return Result("incois:" + name, "public", "partial" if failures else "source_native", data, metadata), raw
