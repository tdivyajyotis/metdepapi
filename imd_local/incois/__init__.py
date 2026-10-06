"""Verified public INCOIS products; separate from IMD contracts."""
import math
import csv
import io
import re
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone
from urllib.parse import urlencode

from ..core import CollectionError, Result
from .native import NATIVE, collect_native

BASE = "https://incois.gov.in"
PRODUCTS = {
    "lsf-wave": {"directory": "ww3", "pattern": r"osf/ww3/rsmc_combined_ww3_(\d{8})\.nc",
                 "defaults": ["HS", "T02"], "description": "LSF wave point forecasts"},
    "lsf-current": {"directory": "currents2", "pattern": r"osf/currents2/RSMC_hycom_(\d{8})\.nc",
                    "defaults": ["SSH", "UVEL", "VVEL"], "description": "LSF ocean point forecasts"},
}
PRODUCTS.update({
    "osf-sst": {"directory": "winds", "page_variable": "sstnio", "pattern": r"osf/winds/SST_NIO_(\d{8})\.nc", "defaults": ["SST"]},
    "osf-mld": {"directory": "winds", "page_variable": "mldnio", "pattern": r"osf/winds/MLD_NIO_(\d{8})\.nc", "defaults": ["MLD", "D20"]},
    "osf-swell": {"directory": "ww3", "pattern": r"osf/ww3/rsmc_combined_ww3_(\d{8})\.nc", "defaults": ["PHS01", "PTP01"]},
    **NATIVE,
})


def xml(text):
    try:
        return ET.fromstring(text)
    except ET.ParseError as exc:
        raise CollectionError("invalid_response", "INCOIS returned invalid XML") from exc


def tag(element):
    return element.tag.rsplit("}", 1)[-1]


def instant(value):
    dt = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    if dt.tzinfo is None:
        raise ValueError("Forecast time lacks timezone")
    return dt.astimezone(timezone.utc)


def time_axis(value):
    """Expand advertised UTC times with a strict size bound."""
    try:
        if "/" not in value:
            result = [instant(v) for v in value.split(",")]
        else:
            start, end, period = value.strip().split("/")
            match = re.fullmatch(r"P(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?", period)
            if not match:
                raise ValueError()
            days, hours, minutes, seconds = [int(v or 0) for v in match.groups()]
            step = timedelta(days=days, hours=hours, minutes=minutes, seconds=seconds)
            first, last = instant(start), instant(end)
            if step.total_seconds() <= 0 or last < first:
                raise ValueError()
            count = int((last - first) / step) + 1
            if count > 512:
                raise ValueError()
            result = [first + i * step for i in range(count)]
        if not result or len(result) > 512 or result != sorted(set(result)):
            raise ValueError()
        return result
    except (ValueError, OverflowError) as exc:
        raise CollectionError("invalid_response", "Invalid or excessive INCOIS forecast time axis") from exc


def latest_dataset(text, spec):
    matches = []
    for node in xml(text).iter():
        path = node.attrib.get("urlPath", "")
        match = re.fullmatch(spec["pattern"], path)
        if match:
            try:
                datetime.strptime(match[1], "%Y%m%d")
            except ValueError:
                continue
            modified = next((c.text for c in node if tag(c) == "date" and c.attrib.get("type") == "modified"), None)
            matches.append((match[1], path, modified))
    if not matches:
        raise CollectionError("unavailable", "No matching INCOIS dataset advertised")
    return max(matches)


def layer_metadata(text):
    layers = {}
    def visit(node, inherited):
        dimensions = dict(inherited)
        for child in node:
            if tag(child) == "Dimension":
                dimensions[child.attrib.get("name")] = (child.text or "").strip()
        name = next((c.text for c in node if tag(c) == "Name"), None)
        if tag(node) == "Layer" and name:
            layers[name] = {
                "title": next((c.text for c in node if tag(c) == "Title"), None),
                "dimensions": dimensions,
            }
        for child in node:
            visit(child, dimensions)
    visit(xml(text), {})
    if not layers:
        raise CollectionError("invalid_response", "INCOIS capabilities lack named layers")
    return layers


def attributes(text):
    if not text.lstrip().startswith("Attributes {"):
        raise CollectionError("invalid_response", "INCOIS variable metadata is not a DAS response")
    result = {}
    for name, body in re.findall(r"(?m)^\s*(\w+)\s*\{([^{}]*)\}", text):
        strings = dict(re.findall(r'String\s+(\w+)\s+"([^"\n]*)"\s*;', body))
        missing = [float(v) for v in re.findall(r"(?:Float32|Float64)\s+(?:missing_value|_FillValue)\s+([-+\deE.]+)\s*;", body)]
        result[name] = {"units": strings.get("units"), "long_name": strings.get("long_name"),
                        "standard_name": strings.get("standard_name"), "missing_values": missing}
    return result


def point_value(text, layer, valid_time, lat, lon, missing):
    fields = dict(re.findall(r"(?m)^\s*(Longitude|Latitude|Layer|Time|Value):\s*([^\r\n]+)", text))
    try:
        if fields.get("Layer") != layer or instant(fields["Time"]) != valid_time:
            raise ValueError()
        if abs(float(fields["Latitude"]) - lat) > 1e-5 or abs(float(fields["Longitude"]) - lon) > 1e-5:
            raise ValueError()
        raw = fields["Value"].strip()
        if raw.lower() in ("nan", "n/a", "none", "null"):
            return None, raw
        value = float(raw)
        if not math.isfinite(value) or any(math.isclose(value, v, rel_tol=1e-6) for v in missing):
            return None, raw
        return value, raw
    except (KeyError, ValueError) as exc:
        raise CollectionError("invalid_response", "INCOIS point response has unexpected identity or format") from exc


def source_unit(metadata):
    if metadata.get("units"):
        return metadata["units"], "DAS.units"
    match = re.search(r"\((m/s|m|s|rad|Hz|Deg|deg\.|Deg\. C\.)\)", metadata.get("long_name") or "")
    return (match[1], "DAS.long_name") if match else (None, "unknown")


def distance_km(lat, lon, other_lat, other_lon):
    a, b = math.radians(lat), math.radians(other_lat)
    v = math.sin((b-a)/2)**2 + math.cos(a)*math.cos(b)*math.sin(math.radians(other_lon-lon)/2)**2
    return 6371.0088 * 2 * math.asin(min(1, math.sqrt(v)))


def subset_rows(text, name, selected, lat, lon, missing, max_distance):
    records = list(csv.DictReader(io.StringIO(text)))
    if not records:
        raise CollectionError("invalid_response", "Empty INCOIS numerical subset")
    keys = list(records[0])
    try:
        latitude = next(k for k in keys if k.startswith("latitude["))
        longitude = next(k for k in keys if k.startswith("longitude["))
        variable = next(k for k in keys if k == name or k.startswith(name + "["))
        found = {}
        for record in records:
            when = instant(record["time"])
            if when in found or when not in selected:
                raise ValueError()
            y, x = float(record[latitude]), float(record[longitude])
            if not math.isfinite(x) or not math.isfinite(y) or not -90 <= y <= 90 or not -180 <= x <= 180:
                raise ValueError()
            distance = distance_km(lat, lon, y, x)
            native = record[variable]
            value = float(native) if native.strip().lower() not in {"nan", "n/a", "", "null"} else None
            if value is not None and (not math.isfinite(value) or any(math.isclose(value, v, rel_tol=1e-6) for v in missing)):
                value = None
            status = "available" if value is not None else "missing"
            if distance > max_distance:
                value, status = None, "sampling_distance_exceeded"
            found[when] = {"layer": name, "valid_time": when.isoformat(), "value": value,
                           "source_value": native, "status": status,
                           "grid_cell_coordinates": {"lat": y, "lon": x}, "grid_cell_distance_km": distance}
        if set(found) != set(selected):
            raise CollectionError("incomplete_response", "INCOIS subset lacks requested forecast times")
        return [found[t] for t in selected]
    except (StopIteration, ValueError, KeyError, TypeError) as exc:
        raise CollectionError("invalid_response", "Unexpected INCOIS numerical subset identity or format") from exc


def normalize(rows):
    """Project variables with explicit units; native rows always retained."""
    mapping = {("HS", "m"): "hs_m", ("T02", "s"): "tm02_s", ("PWP", "s"): "tp_s",
               ("MLD", "m"): "mld_m", ("D20", "m"): "d20_m", ("SSH", "m"): "ssh_m",
               ("SST", "Deg. C."): "sst_c", ("PHS01", "m"): "swell_height_m", ("PTP01", "s"): "swell_period_s"}
    by_time = {}
    for row in rows:
        row["normalized"] = {}
        key = mapping.get((row["layer"], row["units"]))
        if key:
            row["normalized"][key] = row["value"]
        if row["layer"] == "DIR" and row["units"] == "rad":
            row["normalized"]["wave_dir_deg"] = math.degrees(row["value"]) if row["value"] is not None else None
            row["normalization"] = "radians_to_degrees; direction_from/to_convention_unknown"
        by_time.setdefault(row["valid_time"], {})[row["layer"]] = row
    derived = []
    for when, values in by_time.items():
        for u, v, name in (("UWND", "VWND", "wind_speed_ms"), ("UVEL", "VVEL", "current_speed_ms")):
            a, b = values.get(u), values.get(v)
            if a and b and a["units"] == b["units"] == "m/s" and a["value"] is not None and b["value"] is not None and a.get("grid_cell_coordinates") and a.get("grid_cell_coordinates") == b.get("grid_cell_coordinates"):
                derived.append({"valid_time": when, "variable": name, "value": math.hypot(a["value"], b["value"]), "source_layers": [u, v], "method": "hypot; matched returned grid cells"})
    return derived


def collect_incois(product, params, transport, now=None, directory="data/artifacts"):
    if product not in PRODUCTS:
        raise CollectionError("unknown_product", "INCOIS product not registered")
    if product in NATIVE:
        return collect_native(product, params, transport, directory)
    if set(params) - {"lat", "lon", "layers", "steps", "start", "sampling", "max_distance_km"}:
        raise CollectionError("invalid_parameters", "INCOIS accepts lat, lon, layers, steps and start")
    try:
        lat, lon = float(params["lat"]), float(params["lon"])
        steps = int(params.get("steps", "4"))
        sampling = params.get("sampling", "wms")
        max_distance = float(params.get("max_distance_km", "20"))
        if sampling not in {"wms", "ncss"} or not math.isfinite(max_distance) or not 0 < max_distance <= 100:
            raise ValueError()
        if "max_distance_km" in params and sampling != "ncss":
            raise ValueError()
        if not math.isfinite(lat) or not math.isfinite(lon) or not -90 < lat < 90 or not -180 < lon < 180 or not 1 <= steps <= 56:
            raise ValueError()
        start = instant(params["start"]) if "start" in params else (now or datetime.now(timezone.utc))
    except (KeyError, ValueError, TypeError) as exc:
        raise CollectionError("invalid_parameters", "Finite lat/lon, 1..56 steps and a timezone-qualified start are required") from exc
    spec = PRODUCTS[product]
    requested = params.get("layers", ",".join(spec["defaults"])).split(",")
    if not requested or len(requested) > 8 or len(set(requested)) != len(requested) or any(not re.fullmatch(r"[A-Za-z0-9_]+(?::[A-Za-z0-9_]+-[a-z]+)?", v) for v in requested):
        raise CollectionError("invalid_parameters", "Use up to eight unique source layer names")
    raw = {}
    def text(url):
        raw[url] = transport.text(url)
        return raw[url]
    catalog_url = BASE + "/thredds/catalog/osf/" + spec["directory"] + "/catalog.xml"
    if spec.get("page_variable"):
        page = text(BASE + "/oceanservices/osfforecast.jsp")
        match = re.search(r'var\s+' + spec["page_variable"] + r'\s*=\s*"([^"]+)"', page)
        path = "osf/winds/" + match[1] if match else ""
        match = re.fullmatch(spec["pattern"], path)
        if not match:
            raise CollectionError("invalid_response", "OSF page no longer advertises the expected dataset")
        date_label, modified = match[1], None
    else:
        date_label, path, modified = latest_dataset(text(catalog_url), spec)
    wms = BASE + "/thredds/wms/" + path
    layers = layer_metadata(text(wms + "?service=WMS&version=1.3.0&request=GetCapabilities"))
    das = attributes(text(BASE + "/thredds/dodsC/" + path + ".das"))
    if any(name not in layers for name in requested):
        raise CollectionError("invalid_parameters", "Requested layer is not advertised by this dataset")
    rows, failures, variables = [], [], {}
    for name in requested:
        description = layers[name]
        variables[name] = {**description, "attributes": das.get(name, {})}
        unit, unit_basis = source_unit(das.get(name, {}))
        axis = time_axis(description["dimensions"].get("time", ""))
        selected = [t for t in axis if t >= start][:steps]
        if not selected:
            failures.append({"layer": name, "status": "no_forecast_at_or_after_start"})
            continue
        elevation = description["dimensions"].get("elevation")
        if elevation and "0.0" not in [v.strip() for v in elevation.split(",")] and "0" not in [v.strip() for v in elevation.split(",")]:
            raise CollectionError("invalid_response", "Requested layer does not advertise a surface level")
        if sampling == "ncss":
            query = {"var": name, "latitude": lat, "longitude": lon, "time_start": selected[0].isoformat(), "time_end": selected[-1].isoformat(), "accept": "csv"}
            if elevation:
                query["vertCoord"] = "0"
            url = BASE + "/thredds/ncss/grid/" + path + "?" + urlencode(query)
            try:
                subset = subset_rows(text(url), name, selected, lat, lon, das.get(name, {}).get("missing_values", []), max_distance)
                rows.extend({**row, "units": unit, "unit_basis": unit_basis} for row in subset)
            except CollectionError as exc:
                failures.append({"layer": name, "status": exc.status})
                rows.extend({"layer": name, "valid_time": t.isoformat(), "value": None, "source_value": None, "units": unit, "unit_basis": unit_basis, "status": exc.status} for t in selected)
            continue
        for valid in selected:
            query = {"service": "WMS", "version": "1.3.0", "request": "GetFeatureInfo",
                     "layers": name, "query_layers": name, "crs": "CRS:84",
                     "bbox": f"{lon-0.001},{lat-0.001},{lon+0.001},{lat+0.001}",
                     "width": 1, "height": 1, "i": 0, "j": 0,
                     "time": valid.isoformat(), "info_format": "text/plain"}
            if elevation:
                query["elevation"] = "0"
            url = wms + "?" + urlencode(query)
            try:
                value, native = point_value(text(url), name, valid, lat, lon, das.get(name, {}).get("missing_values", []))
                row = {"layer": name, "valid_time": valid.isoformat(), "value": value,
                       "source_value": native, "units": unit, "unit_basis": unit_basis,
                       "status": "available" if value is not None else "missing"}
            except CollectionError as exc:
                failures.append({"layer": name, "valid_time": valid.isoformat(), "status": exc.status})
                row = {"layer": name, "valid_time": valid.isoformat(), "value": None,
                       "source_value": None, "units": unit, "unit_basis": unit_basis, "status": exc.status}
            rows.append(row)
    available = [row for row in rows if row["value"] is not None]
    if not available:
        raise CollectionError("unavailable", "INCOIS has no usable point values for the requested window")
    metadata = {"parameters": params, "publisher": "INCOIS", "retrieved_at": datetime.now(timezone.utc).isoformat(),
                "kind": "forecast", "derived_series": normalize(rows),
                "compatibility": "source_native_only; not_an_official_API_response", "schema_version": 1,
                "source_urls": list(raw), "dataset_path": path, "dataset_date_label": date_label,
                "dataset_modified_at": modified, "issue_time": None, "variables": variables,
                "sampling": {"method": "publisher_NCSS_point_subset" if sampling == "ncss" else "publisher_WMS_GetFeatureInfo", "requested_lat": lat, "requested_lon": lon,
                             "max_distance_km": max_distance if sampling == "ncss" else None,
                             "grid_cell_coordinates": None, "grid_cell_distance_km": None,
                             "notes": "Actual NCSS cells and distances are recorded per row; WMS coordinates identify only the clicked point. Interpolation is not established."},
                "failed_requests": failures, "forecast_end": max(row["valid_time"] for row in available),
                "notes": ["Filename date is a dataset label, not a verified issue time.",
                          "Units come from explicit DAS units or recognized unit annotations in DAS long_name, with unit_basis; otherwise null.",
                          "Surface level 0 requested only where advertised. SSH is not a tide prediction."]}
    status = "partial" if failures or len(available) != len(rows) else "source_native"
    return Result("incois:" + product, "public", status, rows, metadata), raw


def forecast_quality(snapshot, max_age=86400, now=None):
    now = now or datetime.now(timezone.utc)
    metadata = snapshot["metadata"]
    if metadata.get("kind") and metadata["kind"] != "forecast":
        from ..operations import freshness
        quality = freshness(snapshot, max_age=max_age, now=now)
        quality["validity_status"] = metadata.get("validity_status", "not_established")
        quality["unavailable"] = metadata.get("availability") == "no_series"
        return quality
    age = max(0, (now - instant(metadata["retrieved_at"])).total_seconds())
    ends = [instant(row["valid_time"]) for row in snapshot["data"] if row.get("value") is not None]
    expired = not ends or max(ends) < now
    return {"retrieval_age_seconds": age, "retrieval_stale": age > max_age,
            "forecast_expired": expired, "issue_time_status": "unknown",
            "stale": age > max_age or expired}
