"""Assign public AWS points to public IMD state polygons, with provenance."""
import json
import time
from pathlib import Path
from urllib.parse import urlencode

from .core import CATALOG, CollectionError
from .products import WFS


def inside_ring(x, y, ring):
    inside = False
    previous = ring[-1]
    for current in ring:
        x1, y1 = previous[:2]; x2, y2 = current[:2]
        if (y1 > y) != (y2 > y) and x < (x2-x1) * (y-y1) / (y2-y1) + x1:
            inside = not inside
        previous = current
    return inside


def polygon_contains(x, y, polygon):
    return bool(polygon) and inside_ring(x, y, polygon[0]) and not any(inside_ring(x, y, hole) for hole in polygon[1:])


def state_index(payload, name_field="stname"):
    if not isinstance(payload, dict) or payload.get("type") != "FeatureCollection":
        raise CollectionError("invalid_response", "State boundary feed missing")
    index = []
    for feature in payload.get("features", []):
        geometry, properties = feature.get("geometry") or {}, feature.get("properties") or {}
        polygons = geometry.get("coordinates", [])
        if geometry.get("type") == "Polygon": polygons = [polygons]
        elif geometry.get("type") != "MultiPolygon": continue
        for polygon in polygons:
            if not polygon or not polygon[0]: continue
            xs, ys = zip(*(point[:2] for point in polygon[0]))
            index.append((min(xs), min(ys), max(xs), max(ys), properties.get(name_field), polygon))
    if not index:
        raise CollectionError("invalid_response", "State feed contains no polygon boundaries")
    return index


def assign_states(rows, payload, name_field="stname", output_field="STATE"):
    index = state_index(payload, name_field)
    for row in rows:
        x, y = row.get("Longitude"), row.get("Latitude")
        if x is None or y is None: continue
        matches = {name for minx,miny,maxx,maxy,name,polygon in index
                   if minx <= x <= maxx and miny <= y <= maxy and polygon_contains(x,y,polygon)}
        if len(matches) == 1:
            row[output_field] = matches.pop()


def load_states(transport, districts=False):
    url = WFS + "?" + urlencode({"service": "WFS", "version": "1.1.0", "request": "GetFeature",
                                "typeName": "imd:india_districts" if districts else "imd:India_State",
                                "outputFormat": "application/json"})
    cache = Path("data/source-cache/india-districts.json" if districts else "data/source-cache/india-state.json")
    name_field = "district" if districts else "stname"
    if cache.exists() and time.time() - cache.stat().st_mtime < 30 * 86400:
        try:
            value = json.loads(cache.read_text(encoding="utf-8")); state_index(value, name_field)
            return value, url
        except (OSError, ValueError, CollectionError): pass
    value = transport.json(url, max_bytes=64*1024*1024)
    state_index(value, name_field)
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(value), encoding="utf-8")
    return value, url


def state_matches(name, requested):
    key = lambda value: str(value).upper().replace("_", " ").replace("&", "AND").strip()
    aliases = {"NCT OF DELHI": "DELHI", "NCT DELHI": "DELHI", "ORISSA": "ODISHA",
               "ANDAMAN AND NICOBAR ISLANDS": "ANDAMAN NICOBAR", "UTTARANCHAL": "UTTARAKHAND",
               "PONDICHERRY": "PUDUCHERRY"}
    return aliases.get(key(name), key(name)) == key(requested)


def assign_districts(rows, transport):
    """Query only attributes at points; detailed national polygons exceed 64 MiB."""
    cache_file = Path("data/source-cache/district-points.json")
    try:
        cache = json.loads(cache_file.read_text(encoding="utf-8"))
    except (OSError, ValueError): cache = {}
    failures = []
    for row in rows:
        x, y = row.get("Longitude"), row.get("Latitude")
        if not isinstance(x, (int,float)) or not isinstance(y, (int,float)) or not -180<=x<=180 or not -90<=y<=90:
            continue
        key = f"{x:.8f},{y:.8f}"
        entry = cache.get(key)
        if not isinstance(entry, dict) or entry.get("lookup_version") != 3 or time.time() - entry.get("retrieved_at",0) > 30*86400:
            # WFS 1.0 uses longitude/latitude for EPSG:4326 bounding boxes.
            url = WFS + "?" + urlencode({"service":"WFS", "version":"1.0.0", "request":"GetFeature",
                                          "typeName":"imd:india_districts", "outputFormat":"application/json",
                                          "propertyName":"district,state", "maxFeatures":2,
                                          "bbox":f"{x-1e-8},{y-1e-8},{x+1e-8},{y+1e-8},EPSG:4326"})
            try:
                payload = transport.json(url)
                if not isinstance(payload,dict) or not isinstance(payload.get("features"),list):
                    raise CollectionError("invalid_response","District point lookup changed format")
                values = {feature.get("properties",{}).get("district") for feature in payload["features"]} - {None}
                matched = payload.get("numberMatched",payload.get("totalFeatures",0))
                name = next(iter(values)) if len(values)==1 and isinstance(matched,int) and matched<=2 else None
                entry = {"district":name, "retrieved_at":time.time(), "source_url":url, "lookup_version":3}
                cache[key] = entry
            except CollectionError as exc:
                failures.append({"id":row.get("ID"),"status":exc.status})
                continue
        row["DISTRICT"] = entry.get("district")
    cache_file.parent.mkdir(parents=True,exist_ok=True)
    cache_file.write_text(json.dumps(cache),encoding="utf-8")
    return failures
