"""Station-issued IMD event times at exact published station coordinates."""
import math
import re

from .core import CollectionError, PUBLIC_ROOT, Result
from .extended_products import wfs_url
from .feeds import features


def collect_sunmoon(params, transport):
    if set(params) != {"lat", "lon"}:
        raise CollectionError("invalid_parameters", "Sunmoon requires documented lat and lon")
    try:
        lat, lon = float(params["lat"]), float(params["lon"])
        if not math.isfinite(lat) or not math.isfinite(lon) or not -90 <= lat <= 90 or not -180 <= lon <= 180:
            raise ValueError()
    except (TypeError, ValueError) as exc:
        raise CollectionError("invalid_parameters", "Invalid coordinates") from exc
    url = wfs_url("synop_data_layer", ["station_id", "station", "latitude1", "longitude1"])
    registry = transport.json(url)
    candidates = []
    for feature in features(registry):
        source = feature["properties"]
        try:
            if abs(float(source["latitude1"])-lat) <= 1e-7 and abs(float(source["longitude1"])-lon) <= 1e-7:
                candidates.append(str(source["station_id"]))
        except (ValueError, TypeError, KeyError): continue
    if len(candidates) != 1:
        raise CollectionError("unavailable", "Public astronomy is available only at an exact, unambiguous IMD station coordinate; no interpolation")
    city_url = PUBLIC_ROOT + "api/fetchCity_static.php"
    raw = transport.json(city_url, form={"ID": candidates[0]}, headers={"Referer": PUBLIC_ROOT})
    record = raw[0] if isinstance(raw, list) and raw else raw
    if not isinstance(record, dict) or str(record.get("station_id")) != candidates[0]:
        raise CollectionError("invalid_response", "Astronomy station identity mismatch")
    try:
        if abs(float(record["lat"])-lat) > 1e-7 or abs(float(record["lon"])-lon) > 1e-7: raise ValueError()
    except (KeyError, TypeError, ValueError) as exc:
        raise CollectionError("unavailable", "City source coordinates differ; refusing nearest-station astronomy") from exc
    values = {}
    for key in ("sunrise", "sunset", "moonrise", "moonset"):
        value = record.get(key)
        values[key] = value if isinstance(value, str) and re.fullmatch(r"(?:[01]\d|2[0-3]):[0-5]\d", value) else None
    if not any(value is not None for value in values.values()):
        raise CollectionError("unavailable", "Public station has no event times")
    data = {"status": True, "message": "SunMoon Time", "totalCount": None,
            "data": [{key: values[key] for key in ("sunrise", "sunset")},
                     {key: values[key] for key in ("moonrise", "moonset")}]}
    result = Result("sunmoon", "public", "partial" if any(value is None for value in values.values()) else "mapped", data,
                    {"source_url": city_url, "source_urls": [url, city_url], "station_id": candidates[0],
                     "observation_date": record.get("dat"), "astronomy_validity_date": None, "time_zone": "Asia/Kolkata",
                     "missing_fields": [key for key,value in values.items() if value is None],
                     "notes": ["IMD station-issued times at exact source coordinates. Source astronomy validity date is unknown; observation date is separate. No local calculation or coordinate interpolation."]})
    return result, {"registry": registry, "city": raw}
