"""Public sources mapped strictly to the field names in the IMD reference."""
import base64
import json
import re
from datetime import datetime, timedelta
from urllib.parse import urlencode, urljoin

from .core import CATALOG, CollectionError, Result
from .feeds import features
from .products import MAUSAM, WFS, plain
from .artifacts import PAGES, port_arrays

WARNING_TEXT = {"1": "No Warning", "2": "Heavy Rain", "3": "Heavy Snow", "4": "Thunderstorm & Lightning",
                "5": "Hailstorm", "6": "Dust Storm", "7": "Dust Raising Winds", "8": "Strong Surface Winds",
                "9": "Heat Wave", "91": "Severe Heat Wave", "10": "Hot Day", "11": "Warm Night",
                "12": "Cold Wave", "121": "Severe Cold Wave", "13": "Cold Day", "131": "Severe Cold Day",
                "14": "Ground Frost", "15": "Fog", "151": "Dense Fog", "152": "Very Dense Fog",
                "16": "Very Heavy Rain", "17": "Extremely Heavy Rain", "18": "Hot and Humid",
                **{str(value): "Thunderstorm & Lightning" for value in range(41, 47)}}
# Canonical API strings: the reference uses #FFFF00 for yellow. The public WMS
# palette shades differ, but the codes describe the same warning/distribution levels.
WARNING_COLORS = {0: "#FFFFFF", 1: "#FF0000", 2: "#FFA500", 3: "#FFFF00", 4: "#00FF00"}
DISTRIBUTIONS = {1: ("#004de6", "Widespread", "Stations [76-100]%"),
                 2: ("#66FFFF", "Fairly Widespread", "Stations [51-75]%"),
                 3: ("#00b31e", "Scattered", "Stations [26-50]%"),
                 4: ("#4dff4d", "Isolated", "Stations [1-25]%"),
                 5: ("#ffffff", "Dry", "No rain")}


def finish(product, records, source_urls, times, **extra):
    if not records:
        raise CollectionError("unavailable", "No public product records found")
    missing = {str(index): [key for key, value in row.items() if value is None] for index, row in enumerate(records)}
    return Result(product, "public", "partial" if any(missing.values()) else "mapped", records,
                  {"source_urls": source_urls, "source_url": source_urls[0], "missing_fields": missing,
                   "record_issue_times": times, "compatibility": "IMD_reference_field_contract",
                   **extra})


def wfs_url(layer, properties):
    return WFS + "?" + urlencode({"service": "WFS", "version": "1.1.0", "request": "GetFeature",
                                  "typeName": "imd:" + layer, "outputFormat": "application/json",
                                  "propertyName": ",".join(properties)})


def map_subdivision(product, payload):
    records, times = [], []
    for feature in features(payload):
        raw = feature["properties"]
        if not raw.get("SUBDIV") or not raw.get("Date"):
            raise CollectionError("invalid_response", "Subdivision identity/date missing")
        row = dict.fromkeys(CATALOG[product]["documented_fields"])
        row.update({"date_obs": raw["Date"], "SUBDIV": raw["SUBDIV"]})
        days = 5 if product == "subdivisionwarning" else 7
        for day in range(1, days + 1):
            if product == "subdivisionwarning":
                codes = str(raw.get(f"Day_{day}", "")).split(",")
                codes = [code.strip() for code in codes if code.strip()]
                if codes and all(code in WARNING_TEXT for code in codes):
                    labels = list(dict.fromkeys(WARNING_TEXT[code] for code in codes if code != "1"))
                    row[f"day{day}_warning"] = " and ".join(labels) if labels else "No Warning"
                row[f"day{day}_color"] = WARNING_COLORS.get(raw.get(f"Day{day}_Color"))
            else:
                values = DISTRIBUTIONS.get(raw.get(f"Day_{day}"), (None, None, None))
                for suffix, value in zip(("color", "distribution", "distribution_percentage"), values):
                    row[f"day{day}_{suffix}"] = value
        records.append(row)
        times.append({"identity": raw["SUBDIV"], "date": raw["Date"], "updated_at": raw.get("updat")})
    return records, times


def forecast_areas(text):
    match = re.search(r'"areas"\s*:\s*(\[)', text)
    if not match:
        raise CollectionError("invalid_response", "District forecast areas missing")
    try:
        areas, _ = json.JSONDecoder().raw_decode(text[match.start(1):])
    except ValueError as exc:
        raise CollectionError("invalid_response", "District forecast map changed format") from exc
    result = {}
    for area in areas:
        identity = str(area.get("id", ""))
        # Some legacy maps retain empty placeholder areas; do not turn them into forecasts.
        if not identity or not area.get("title"):
            continue
        balloon = area.get("balloonText", "").replace("</br>", "<br>")
        parts = [plain(value) for value in re.split(r'<br\s*/?>', balloon, flags=re.I)]
        date = re.findall(r"\d{4}-\d{2}-\d{2}", balloon)
        if len(parts) < 3 or not date:
            raise CollectionError("invalid_response", "District forecast date/distribution missing")
        result[identity] = {"name": area["title"], "date": date[-1], "color": area.get("color"),
                            "distribution": parts[1], "percentage": parts[2]}
    if not result:
        raise CollectionError("unavailable", "District forecast has no records")
    return result


def map_district_forecast(pages, geography):
    state_by_id = {str(feature["properties"].get("Obj_id", feature["properties"].get("obj_id"))):
                   feature["properties"].get("State", feature["properties"].get("state"))
                   for feature in features(geography)
                   if feature["properties"].get("Obj_id", feature["properties"].get("obj_id")) not in (None, 0)}
    states_by_name = {}
    name_key = lambda value: re.sub(r"[^A-Z0-9]", "", str(value).upper())
    for feature in features(geography):
        value = feature["properties"]
        states_by_name.setdefault(name_key(value.get("District", value.get("district"))), set()).add(value.get("State", value.get("state")))
    periods = {day: forecast_areas(page) for day, page in pages.items()}
    records, times, inconsistent = [], [], []
    for identity in sorted(set().union(*(set(rows) for rows in periods.values()))):
        available = [rows[identity] for rows in periods.values() if identity in rows]
        first = available[0]
        candidates = states_by_name.get(name_key(first["name"]), set()) - {None}
        state = state_by_id.get(identity) or (next(iter(candidates)) if len(candidates) == 1 else None)
        row = dict.fromkeys(CATALOG["state_district_rainfall_forecast"]["documented_fields"])
        row.update({"date_obs": first["date"], "Obj_id": identity, "District": first["name"],
                    "State": state})
        for day, entries in periods.items():
            value = entries.get(identity)
            expected_date = (datetime.strptime(first["date"], "%Y-%m-%d") + timedelta(days=int(day)-1)).strftime("%Y-%m-%d")
            # The balloon dates are forecast validity dates and advance each day.
            if value is None or value["date"] != expected_date or value["name"] != first["name"]:
                inconsistent.append({"id": identity, "day": day, "reason": "missing_or_mixed_issue"})
                continue
            row[f"day{day}_color"] = value["color"]
            row[f"day{day}_distribution"] = value["distribution"]
            percent = re.fullmatch(r"Stations\s*=\s*(\d+-\d+)%", value["percentage"])
            row[f"day{day}_distribution_percentage"] = f"Stations [{percent[1]}]%" if percent else value["percentage"]
        records.append(row)
        times.append({"identity": identity, "date": first["date"], "date_kind": "first_forecast_date",
                      "forecast_dates": {str(day): values[identity]["date"] for day, values in periods.items() if identity in values}})
    return records, times, inconsistent


def map_ports(text):
    rows = port_arrays(text)
    match = re.search(r'var\s+id_array\s*=\s*(\[.*?\]);', text, re.S)
    try:
        ids = json.loads(match[1])
        if len(ids) != len(rows):
            raise ValueError()
        ids = [base64.b64decode(value, validate=True).decode("ascii") for value in ids]
    except (ValueError, TypeError, UnicodeError) as exc:
        raise CollectionError("invalid_response", "Port source identities changed format") from exc
    records, times = [], []
    for identity, raw in zip(ids, rows):
        try:
            date = datetime.strptime(raw["date"], "%d-%m-%Y %H:%M:%S").strftime("%Y-%m-%d")
        except ValueError:
            date = None
        row = {"Port Id": identity, "Port Name": raw["name"], "Issued By": None,
               "Date of Issue": date, "Warning": raw["warning"] or raw["message"] or None}
        records.append(row)
        times.append({"identity": identity, "date": date, "source_date_time": raw["date"]})
    return records, times


def map_bulletin(text, product):
    clean = re.sub(r'<!--.*?-->|<script\b.*?</script>|<style\b.*?</style>', '', text, flags=re.S | re.I)
    content = plain(clean)
    issued_by = re.search(r'\b(?:ACWC|CWC)\s+[A-Z]+', content)
    validity = re.search(r'Valid for\s+(\d+)\s*hrs?\s*from\s+(\d{1,2}(?::\d{2})?)\s*UTC of\s*(\d{4}-\d{2}-\d{2})', content, re.I)
    updated = re.search(r'Time of Issue\s*(\d{2}:\d{2})\s*IST of\s*(\d{4}-\d{2}-\d{2})', content, re.I)
    if not validity:
        raise CollectionError("invalid_response", "Bulletin validity period missing")
    tables, common = [], {}
    for table in re.findall(r'<table\b[^>]*>(.*?)</table>', clean, re.I | re.S):
        caption = re.search(r'<caption\b[^>]*>(.*?)</caption>', table, re.I | re.S)
        values = {}
        for tr in re.findall(r'<tr\b[^>]*>(.*?)</tr>', table, re.I | re.S):
            cells = re.findall(r'<t[dh]\b[^>]*>(.*?)</t[dh]>', tr, re.I | re.S)
            if len(cells) == 2:
                values[plain(cells[0])] = plain(cells[1])
        if caption:
            tables.append((plain(caption[1]), values))
        else:
            common.update(values)
    records = []
    for name, values in tables:
        row = dict.fromkeys(CATALOG[product]["documented_fields"])
        row.update({key: value for key, value in {**common, **values}.items() if key in row})
        hour = validity[2] if ":" in validity[2] else validity[2].zfill(2) + ":00"
        row.update({"Layer": name, "Issued by": issued_by[0] if issued_by else None,
                    "Valid From": validity[3] + " " + hour + ":00", "Validity": validity[1],
                    "Update Time": updated[2] + " " + updated[1] + ":00" if updated else None})
        # The source provides validity/publication dates, not an observation date or API layer ID.
        records.append(row)
    times = [{"date": updated[2] if updated else validity[3],
              "updated_at": updated[2] + "T" + updated[1] + ":00+05:30" if updated else None}]
    return records, times


def collect_extended(product, params, transport):
    if params:
        # Only portwarning documents an ID filter among these new products.
        if product != "portwarning" or set(params) != {"id"}:
            raise CollectionError("invalid_parameters", "No such query parameter in the IMD reference for this product")
    raw, urls, extra = {}, [], {}
    if product.startswith("subdivision"):
        warning = product == "subdivisionwarning"
        layer = "subdiv_warnings_now" if warning else "subdiv_rainfall_now"
        properties = ["Date", "SUBDIV"] + [f"Day_{day}" for day in range(1, 8)]
        if warning:
            properties += ["updat"] + [f"Day{day}_Color" for day in range(1, 8)]
        url = wfs_url(layer, properties)
        raw = transport.json(url); urls.append(url)
        records, times = map_subdivision(product, raw)
        extra["derived_fields"] = [key for key in CATALOG[product]["documented_fields"] if key.startswith("day")]
    elif product == "state_district_rainfall_forecast":
        pages = {}
        for day in range(1, 6):
            url = MAUSAM + "rainfallinformation/district_rain.php?" + urlencode({"day": f"Day_{day}"})
            pages[day] = transport.text(url); urls.append(url)
        url = wfs_url("NowcastWarningDistrict", ["Obj_id", "District", "State"])
        geography = transport.json(url); urls.append(url)
        records, times, inconsistent = map_district_forecast(pages, geography)
        raw = {"pages": pages, "geography": geography}
        extra["inconsistent_slots"] = inconsistent
    elif product == "portwarning":
        url = PAGES["port"]; raw = transport.text(url); urls.append(url)
        records, times = map_ports(raw)
        from .document_content import document_content
        source_rows = port_arrays(raw)
        documents, failures = {}, []
        for filename in sorted({row["file"] for row in source_rows if row["file"]}):
            source_url = "https://rsmcnewdelhi.imd.gov.in/uploads/archive/57/" + filename
            try:
                documents[filename] = document_content({"url": source_url, "kind": "pdf"}, transport)
            except CollectionError as exc:
                failures.append({"source_url": source_url, "status": exc.status})
        for row, source in zip(records, source_rows):
            text = " ".join(documents.get(source["file"], {}).get("pages", []))
            publisher = re.search(r'\b(?:ACWC|CWC)\s+[A-Z]+|(?:AREA\s+)?CYCLONE WARNING CENTRE\s*,?\s*[A-Z]+', text)
            if publisher:
                row["Issued By"] = " ".join(publisher[0].split())
        raw = {"page": raw, "documents": documents}
        extra["failed_sources"] = failures
        if params:
            records = [row for row in records if row["Port Id"] == params["id"]]
            times = [row for row in times if row["identity"] == params["id"]]
            if not records:
                raise CollectionError("not_found", "Port ID not present in public source")
        extra["identifier_source"] = "Decoded public port map id_array; reference does not enumerate IDs"
    else:
        page = MAUSAM + ("marine_forecast.php" if product == "seabulletin" else "coastal_forecast.php")
        index = transport.text(page); urls.append(page); raw[page] = index
        slug = "seaarea_bulletin_new" if product == "seabulletin" else "coastal_bulletin_new"
        pattern = r'href=["\']([^"\']*' + slug + r'\.php(?:\?[^"\']*)?)["\']'
        targets = sorted({urljoin(page, value) for value in re.findall(pattern, index)})
        if not targets:
            raise CollectionError("unavailable", "Public bulletin links missing")
        records, times, failures = [], [], []
        for url in targets:
            try:
                source = transport.text(url); raw[url] = source; urls.append(url)
                rows, issues = map_bulletin(source, product); records.extend(rows); times.extend(issues)
            except CollectionError as exc:
                failures.append({"source_url": url, "status": exc.status})
        extra.update({"failed_sources": failures, "time_zones": {"Valid From": "UTC", "Update Time": "IST"}})
    result = finish(product, records, urls, times, **extra)
    if extra.get("failed_sources"):
        result.status = "partial"
    return result, raw
