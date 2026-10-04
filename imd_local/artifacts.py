"""Public documents and source-native datasets; never served as API parity."""
import html
import json
import math
import re
import csv
import io
from datetime import datetime, timezone
from urllib.parse import urlencode, urljoin, urlsplit, urlunsplit

from .core import CollectionError, Result
from .feeds import features
from .products import MAUSAM, WFS, plain

PAGES = {
    "national": MAUSAM + "all_india_forcast_bulletin.php",
    "marine": MAUSAM + "marine_forecast.php",
    "coastal": MAUSAM + "coastal_forecast.php",
    "highway": MAUSAM + "highwayForecast.php",
    "agromet": MAUSAM + "agromet_adv_ser_state_current.php",
    "radar": MAUSAM + "radar.php",
    "district-forecast": MAUSAM + "5d_statewisedistricts_rf_forecast.php",
    "port": "https://rsmcnewdelhi.imd.gov.in/port-warning.php",
    "sea": "https://rsmcnewdelhi.imd.gov.in/sea-area-bulletin.php",
    "coastal-bulletins": "https://rsmcnewdelhi.imd.gov.in/coastal-weather-bulletin.php",
    "fishermen": "https://rsmcnewdelhi.imd.gov.in/fishermen-warning.php",
}
NATIVE_LAYERS = {"subdivision-warning": "subdiv_warnings_now",
                 "subdivision-rainfall": "subdiv_rainfall_now", "cyclone-track": "Cyclone_Track_V",
                 "radar-status": "radar_station_status"}
ARTIFACTS = sorted([*PAGES, *NATIVE_LAYERS, "mausamgram", "lightning", "city-stations", "aws-stations", "cyclone-wind", "cyclone-cone"])


def documents(text, url):
    # Drop shared navigation, news ticker and footer before discovering product links.
    content = re.search(r'<(?:section|div)[^>]+class=["\'][^"\']*page-content', text, re.I)
    start = content.start() if content else text.find('<main')
    if start >= 0:
        text = text[start:]
    text = re.split(r'<footer\b|<!--\s*footer|<(?:section|div)[^>]+class=["\'][^"\']*footer', text, flags=re.I)[0]
    links = []
    for value in re.findall(r'(?:href|src|data)\s*=\s*["\']([^"\']+)', text, re.I):
        absolute = urljoin(url, html.unescape(value))
        parsed = urlsplit(absolute)
        if parsed.scheme not in ("https", "http") or not parsed.hostname or not parsed.hostname.endswith(".imd.gov.in"):
            continue
        if not re.search(r'\.(?:pdf|gif|png|jpg|jpeg)$', parsed.path, re.I):
            continue
        if any(piece in parsed.path for piece in ("/img/", "marquee_data", "advertisements", "_sop")):
            continue
        absolute = urlunsplit(parsed._replace(fragment=""))
        if absolute not in links:
            links.append(absolute)
    return [{"url": link, "kind": "pdf" if urlsplit(link).path.lower().endswith(".pdf") else "image",
             "content_verified": False} for link in links]


def port_arrays(text):
    values = {}
    for name in ("name", "latitude", "longtitude", "message", "warning", "file", "date"):
        match = re.search(rf'var\s+{name}_array\s*=\s*(\[.*?\]);', text, re.S)
        if not match:
            raise CollectionError("invalid_response", "Port warning array missing")
        try:
            values[name] = json.loads(match.group(1).replace("\r", " ").replace("\n", " "))
        except ValueError as exc:
            raise CollectionError("invalid_response", "Port warning arrays changed format") from exc
    if len({len(v) for v in values.values()}) != 1:
        raise CollectionError("incomplete_response", "Port warning arrays have different lengths")
    return [{key: rows[index] for key, rows in values.items()} for index in range(len(values["name"]))]


def collect_artifact(name, params, transport, download=True, directory="data/artifacts"):
    if name not in ARTIFACTS:
        raise CollectionError("unknown_product", "Artifact source not registered")
    metadata = {"parameters": params, "compatibility": "source_native_only; not_an_official_API_response"}
    if name == "city-stations":
        if set(params) - {"query"}:
            raise CollectionError("invalid_parameters", "City station discovery accepts an optional query")
        url = "https://city.imd.gov.in/citywx/responsive/api/search.php?" + urlencode({"query": params.get("query", "")})
        raw = transport.json(url); data = raw
    elif name == "aws-stations":
        if params:
            raise CollectionError("invalid_parameters", "AWS station discovery has no filters")
        url = WFS + "?" + urlencode({"service": "WFS", "version": "1.1.0", "request": "GetFeature",
                                    "typeName": "imd:aws_data_layer", "outputFormat": "application/json"})
        raw = transport.json(url)
        data = [{"id": row["properties"].get("id"), "call_sign": row["properties"].get("call_sign"),
                 "station": row["properties"].get("station"), "geometry": row.get("geometry")} for row in features(raw)]
    elif name == "lightning":
        if params:
            raise CollectionError("invalid_parameters", "Lightning source has no filters")
        raw, data, failures = {}, {}, []
        for filename in ("finalnew.csv", "last15.csv", "final0_5.csv", "final5_10.csv", "final10_15.csv"):
            url = "https://dss.imd.gov.in/dwr_img/GIS/" + filename
            try:
                source = transport.text(url)
                rows = list(csv.DictReader(io.StringIO(source)))
                if not rows or not {"Lightning_Time", "La", "Lo"} <= set(rows[0]):
                    raise CollectionError("invalid_response", "Lightning CSV has no expected records")
                raw[url] = source; data[filename] = rows
                metadata.setdefault("record_issue_times", []).extend(
                    {"identity": filename + ":" + str(index), "date": row["Lightning_Time"][:10],
                     "updated_at": row["Lightning_Time"].replace(" ", "T") + "+00:00"}
                    for index, row in enumerate(rows))
            except CollectionError as exc:
                failures.append({"source_url": url, "status": exc.status})
        if not data:
            # The public IMD home page also links a lightning animation. Retain it
            # as an image, never manufacture point detections from its pixels.
            image = {"url": MAUSAM + "lightning/Converted/BT.gif", "kind": "image", "content_verified": False}
            if not download:
                data = {"documents": [image]}
            else:
                from .document_content import document_content
                try:
                    data = {"documents": [document_content(image, transport, directory)]}
                except CollectionError as exc:
                    raise CollectionError("unavailable", "Public lightning point feeds and animation are unavailable") from exc
            metadata["representation"] = "public_lightning_animation; not_point_detections"
            url = image["url"]
        metadata.update({"failed_sources": failures, "source_urls": list(raw)})
    elif name in ("cyclone-wind", "cyclone-cone"):
        if params:
            raise CollectionError("invalid_parameters", "Cyclone native datasets have no filters")
        suffixes = ["27", "34", "50", "64"] if name == "cyclone-wind" else [""]
        raw, data = {}, {}
        for suffix in suffixes:
            url = "https://dss.imd.gov.in/dwr_img/GIS/tout" + suffix + ".geojson"
            payload = transport.json(url); features(payload)
            raw[url] = payload; data[suffix or "cone"] = payload
        metadata["source_urls"] = list(raw)
    elif name == "mausamgram":
        if set(params) != {"lat", "lon"}:
            raise CollectionError("invalid_parameters", "Mausamgram requires lat and lon")
        try:
            lat, lon = float(params["lat"]), float(params["lon"])
            if not math.isfinite(lat) or not math.isfinite(lon) or not -90 <= lat <= 90 or not -180 <= lon <= 180:
                raise ValueError()
        except (TypeError, ValueError) as exc:
            raise CollectionError("invalid_parameters", "Invalid coordinates") from exc
        index_url = "https://mausamgram.imd.gov.in/mmem_3hr.txt"
        index = transport.text(index_url).strip()
        cycle = index.split(",")[0].strip()
        if not re.fullmatch(r"\d{10}", cycle):
            raise CollectionError("invalid_response", "Mausamgram model cycle missing")
        url = "https://mausamgram.imd.gov.in/test4_mme.php?" + urlencode(
            {"lat_gfs": f"{math.floor(lat / 0.125) * 0.125:.3f}",
             "lon_gfs": f"{math.floor(lon / 0.125) * 0.125:.3f}", "date": cycle + "_3hr_0p125"})
        raw = transport.json(url)
        if not isinstance(raw, (list, dict)) or not raw or (isinstance(raw, dict) and "error" in raw):
            raise CollectionError("unavailable", "Mausamgram has no forecast for this point")
        data = raw
        metadata.update({"model_cycle": cycle, "source_urls": [index_url, url], "source_index": index})
    elif name in NATIVE_LAYERS:
        if params:
            raise CollectionError("invalid_parameters", "Native layers have no local filters")
        query = {"service": "WFS", "version": "1.1.0", "request": "GetFeature",
                 "typeName": "imd:" + NATIVE_LAYERS[name], "outputFormat": "application/json"}
        if name.startswith("subdivision-"):
            properties = {"ID", "Date", "SUBDIV"} | {f"Day_{day}" for day in range(1, 8)}
            if name == "subdivision-warning":
                properties |= {"UTC", "updat"} | {f"Day{day}_Color" for day in range(1, 8)} | {f"prob{day}" for day in range(1, 8)} | {f"dist{day}" for day in range(1, 8)}
            query["propertyName"] = ",".join(sorted(properties))
        url = WFS + "?" + urlencode(query)
        raw = transport.json(url)
        features(raw)
        data = raw
        metadata["source_layer"] = NATIVE_LAYERS[name]
        metadata["record_issue_times"] = [{"identity": feature.get("id"),
                                           "date": feature["properties"].get("Date"),
                                           "updated_at": feature["properties"].get("updat")}
                                          for feature in raw["features"]]
    else:
        if params:
            raise CollectionError("invalid_parameters", "Document pages have no local filters")
        url = PAGES[name]
        raw = transport.text(url)
        data = {"documents": documents(raw, url)}
        # Regional product links also appear inside switch-handler JavaScript after the footer.
        if name in ("marine", "agromet"):
            scope = r'(?:backend/assets/(?:cwcv_pdf|acwcc_pdf|cwc_thiru_pdf|acwc_mumbai_pdf|cwc_ahmedabad_pdf)/|imd_latest/contents/agromet/agromet-data/)'
            for value in re.findall(r'["\']([^"\']+\.pdf)(?:#[^"\']*)?["\']', raw, re.I):
                absolute = urljoin(url, html.unescape(value))
                if re.search(scope, urlsplit(absolute).path) and not any(row["url"] == absolute for row in data["documents"]):
                    data["documents"].append({"url": absolute, "kind": "pdf", "content_verified": False})
            if name == "agromet":
                data["documents"] = [row for row in data["documents"] if "/Calendar/" not in row["url"]]
        if name == "port":
            data["documents"] = []
            data["port_records"] = port_arrays(raw)
            files = json.loads(re.search(r'var\s+file_array\s*=\s*(\[.*?\]);', raw, re.S)[1])
            data["documents"] = [{"url": "https://rsmcnewdelhi.imd.gov.in/uploads/archive/57/" + value,
                                  "kind": "pdf", "content_verified": False} for value in sorted(set(files)) if value]
        if name in ("sea", "coastal-bulletins", "fishermen"):
            menu = {"sea": ("59", "60"), "coastal-bulletins": ("49",), "fishermen": ("45",)}[name]
            # RSMC pages have shared navigation; select only the current product archive folders.
            data["documents"] = [{"url": urljoin(url, value), "kind": "pdf", "content_verified": False}
                                 for value in sorted(set(re.findall(r'href=["\'](uploads/archive/(?:' + "|".join(menu) + r')/[^"\']+\.pdf)["\']', raw)))]
        if name == "agromet":
            selection = re.search(r'<select[^>]+onchange=["\']showUser2.*?</select>', raw, re.S)
            states = re.findall(r'<option[^>]*value=["\']([^"\']+)', selection[0]) if selection else []
            state_pages, failures = {}, []
            for state in states:
                source_url = MAUSAM + "agrometinformation/getimageenglish_state.php?" + urlencode({"s": state})
                try:
                    response = transport.text(source_url); state_pages[state] = response
                    data["documents"].extend(documents(response, source_url))
                except CollectionError as exc:
                    failures.append({"state": state, "status": exc.status})
            metadata["failed_states"] = failures
            raw = {"page": raw, "states": state_pages}
        if name == "district-forecast":
            data["map_sources"] = sorted({urljoin(url, link) for link in re.findall(r'"ajaxUrl"\s*:\s*"([^"]+)"', raw)})
            match = re.search(r'"areas"\s*:\s*(\[)', raw)
            if match:
                try:
                    data["map_records"], _ = json.JSONDecoder().raw_decode(raw[match.start(1):])
                except ValueError as exc:
                    raise CollectionError("invalid_response", "District forecast map changed format") from exc
        if not any(data.values()):
            raise CollectionError("unavailable", "No product documents or data found on public page")
        if download and data.get("documents"):
            from .document_content import enrich_documents
            data["documents"], metadata["failed_documents"] = enrich_documents(data["documents"], transport, directory)
        metadata["notes"] = ["PDF text preserves page boundaries; image-only documents are labelled. Text extraction does not establish issue dates or numerical API equivalence."]
    metadata.update({"source_url": url, "retrieved_at": datetime.now(timezone.utc).isoformat()})
    status = "partial" if metadata.get("failed_documents") or metadata.get("failed_states") or metadata.get("failed_sources") else "source_native"
    return Result("artifact:" + name, "public", status, data, metadata), raw
