"""Stored marine inspection view with independent source provenance."""
import math
from ..core import CollectionError
from ..operations import freshness
from . import forecast_quality


def marine_view(store, params, max_age=86400):
    if set(params) - {"lat", "lon", "city_id"}:
        raise CollectionError("invalid_parameters", "Marine view accepts lat, lon and city_id")
    lat, lon = params.get("lat", "19.24"), params.get("lon", "84.94")
    try:
        if not math.isfinite(float(lat)) or not math.isfinite(float(lon)) or not -90 < float(lat) < 90 or not -180 < float(lon) < 180:
            raise ValueError()
    except ValueError as exc:
        raise CollectionError("invalid_parameters", "Invalid marine view coordinates") from exc
    queries = [("imd_city", "cityforecast", {"id": params.get("city_id", "43049")}),
               ("imd_sea", "seabulletin", {}), ("imd_coastal", "coastalbulletin", {})]
    queries += [(name, "incois:" + name, {"lat": lat, "lon": lon, "sampling": "ncss"})
                for name in ("lsf-wave", "lsf-current", "osf-sst", "osf-mld", "osf-swell")]
    queries += [(name, "incois:" + name, {}) for name in ("wave-alerts", "current-alerts", "waman", "pfz", "abis", "hf-radar", "tide-gauge")]
    components = {}
    for label, product, query in queries:
        snapshot = store.latest(product, "public", query)
        if snapshot is None:
            components[label] = {"availability": "missing", "product": product, "parameters": query}
            continue
        quality = forecast_quality(snapshot, max_age) if product.startswith("incois:") else freshness(snapshot, max_age)
        availability = "unavailable" if quality.get("unavailable") else ("stale" if quality["stale"] else "stored")
        components[label] = {"availability": availability, "freshness": quality, "snapshot": snapshot}
    return {"mode": "inspection", "requested_point": {"lat": lat, "lon": lon}, "components": components,
            "notes": "Each component retains its own publisher, location and temporal meaning; observations and forecasts are not substituted."}
