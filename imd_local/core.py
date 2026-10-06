import hashlib
import gzip
import io
import http.client
import json
import os
import sqlite3
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import closing
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

CATALOG = json.loads(Path(__file__).with_name("catalog.json").read_text(encoding="utf-8"))
PUBLIC_ROOT = "https://city.imd.gov.in/citywx/responsive/"


class CollectionError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def tls_context():
    """Portable verified TLS; explicit CA bundle overrides optional OS trust."""
    cafile = os.environ.get("IMD_CA_BUNDLE")
    try:
        if cafile:
            return ssl.create_default_context(cafile=cafile)
        try:
            import truststore
        except ImportError:
            return ssl.create_default_context()
        return truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    except (OSError, ssl.SSLError) as exc:
        raise CollectionError("configuration_error", "Unable to load trusted TLS certificates") from exc


class Transport:
    """Verified TLS, bounded responses, sequential request spacing; no auth bypass."""
    def __init__(self, timeout=25, interval=1.0, retries=2):
        self.timeout = timeout
        self.interval = interval
        self.last_request = 0.0
        self.retries = retries

    def request(self, url, form=None, headers=None, max_bytes=10 * 1024 * 1024, allowed_domains=None):
        for attempt in range(self.retries + 1):
            try:
                return self._request(url, form, headers, max_bytes, allowed_domains)
            except CollectionError as exc:
                if exc.status not in ("network_error", "retryable_upstream") or attempt == self.retries:
                    raise
                time.sleep(min(2 ** attempt, 8))

    def _request(self, url, form=None, headers=None, max_bytes=10 * 1024 * 1024, allowed_domains=None):
        time.sleep(max(0, self.interval - (time.monotonic() - self.last_request)))
        self.last_request = time.monotonic()
        data = urllib.parse.urlencode(form).encode() if form is not None else None
        request_headers = {"User-Agent": "imd-local/0.1 (public weather research)", "Accept": "application/json"}
        request_headers.update(headers or {})
        context = tls_context()
        request = urllib.request.Request(url, data=data, headers=request_headers)
        try:
            if allowed_domains:
                from urllib.parse import urlsplit
                class RestrictedRedirect(urllib.request.HTTPRedirectHandler):
                    def redirect_request(self, req, fp, code, msg, hdrs, newurl):
                        target = urlsplit(newurl)
                        if target.scheme != "https" or not any(target.hostname == d or (target.hostname or "").endswith("." + d) for d in allowed_domains):
                            raise CollectionError("invalid_source", "Document redirect left its publisher hosts")
                        return super().redirect_request(req, fp, code, msg, hdrs, newurl)
                opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context), RestrictedRedirect())
                opened = opener.open(request, timeout=self.timeout)
            else:
                opened = urllib.request.urlopen(request, timeout=self.timeout, context=context)
            with opened as response:
                raw = response.read(max_bytes + 1)
                if len(raw) > max_bytes:
                    raise CollectionError("invalid_response", "Response exceeds configured byte limit")
                if getattr(response, "headers", {}).get("Content-Encoding", "").lower() == "gzip":
                    try:
                        with gzip.GzipFile(fileobj=io.BytesIO(raw)) as zipped:
                            raw = zipped.read(max_bytes + 1)
                    except (OSError, EOFError) as exc:
                        raise CollectionError("invalid_response", "Invalid gzip response") from exc
                    if len(raw) > max_bytes:
                        raise CollectionError("invalid_response", "Expanded response exceeds configured byte limit")
        except urllib.error.HTTPError as exc:
            status = "requires_access" if exc.code in (401, 403) else ("retryable_upstream" if exc.code in (429, 500, 502, 503, 504) else "upstream_error")
            raise CollectionError(status, f"Upstream HTTP {exc.code}") from exc
        except urllib.error.URLError as exc:
            # Do not expose URLs, headers or proxy credentials in exception messages.
            raise CollectionError("network_error", "Upstream connection or TLS verification failed") from exc
        except (OSError, http.client.HTTPException) as exc:
            raise CollectionError("network_error", "Upstream connection timed out or closed") from exc
        return raw

    def text(self, url, **kwargs):
        try:
            return self.request(url, **kwargs).decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            raise CollectionError("invalid_response", "Upstream returned invalid UTF-8") from exc

    def json(self, url, **kwargs):
        try:
            return json.loads(self.request(url, **kwargs))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise CollectionError("invalid_response", "Upstream returned invalid JSON") from exc


@dataclass
class Result:
    product: str
    provider: str
    status: str
    data: object = None
    metadata: dict = field(default_factory=dict)

    def envelope(self):
        return {"product": self.product, "provider": self.provider, "status": self.status,
                "data": self.data, "metadata": self.metadata}


OBSERVATION_MAPPING = {
    "Date": "dat", "Station_Code": "station_id", "Station_Name": "station",
    "Today_Max_temp": "max", "Today_Max_Departure_from_Normal": "maxdep",
    "Previous_Day_Max_temp": "prevday_max", "Previous_Day_Max_Departure_from_Normal": "prevday_maxdep",
    "Today_Min_temp": "min", "Today_Min_Departure_from_Normal": "mindep",
    "Past_24_hrs_Rainfall": "rainfall", "Relative_Humidity_at_0830": "rh0830",
    "Relative_Humidity_at_1730": "rh1730", "Previous_Day_Relative_Humidity_at_1730": "prevday_rh1730",
    "Sunset_time": "sunset", "Sunrise_time": "sunrise", "Moonset_time": "moonset", "Moonrise_time": "moonrise",
}


def map_city(payload, product, station_id):
    records = payload if isinstance(payload, list) else [payload]
    if not records or not isinstance(records[0], dict):
        raise CollectionError("invalid_response", "Missing city weather record")
    raw = records[0]
    if str(raw.get("station_id")) != str(station_id) or not raw.get("dat"):
        raise CollectionError("invalid_response", "Station identity or observation date missing/mismatched")
    try:
        datetime.strptime(raw["dat"], "%Y-%m-%d")
    except (TypeError, ValueError) as exc:
        raise CollectionError("invalid_response", "Invalid observation date") from exc
    mapping = dict(OBSERVATION_MAPPING)
    for slot in range(7):
        prefix = "Todays_Forecast" if slot == 0 else f"Day_{slot + 1}"
        mapping[prefix + "_Max_Temp"] = f"max{slot}"
        mapping[prefix + "_Min_temp"] = f"min{slot}"
        mapping[prefix if slot == 0 else prefix + "_Forecast"] = f"forecast{slot}"
    if product == "cityforecastloc":
        mapping.update({"Latitude": "lat", "Longitude": "lon"})
    # Empty and missing values stay null. Raw numeric strings/sentinels remain unchanged.
    output = {key: raw.get(source) if raw.get(source) != "" else None for key, source in mapping.items()}
    missing = [key for key, value in output.items() if value is None]
    forecast_dates = []
    if len(records) > 1 and isinstance(records[1], dict):
        forecast_dates = sorted(k for k, v in records[1].items()
                                if isinstance(v, dict) and v.get("F_MAX") is not None)
    metadata = {
        "observation_date": raw["dat"], "upstream_updated_at": raw.get("updat"),
        "missing_fields": missing, "forecast_dates_from_source": forecast_dates,
        "compatibility": "documented_field_names; wire_types_and_forecast_alignment_unverified",
        "notes": ["Day slots retain the website order; do not infer dates from observation Date.",
                  "Raw numeric strings and missing-value sentinels are preserved."],
    }
    return Result(product, "public", "partial" if missing else "mapped", [output], metadata)


def collect(product, params, provider="public", transport=None):
    if product not in CATALOG:
        raise CollectionError("unknown_product", "Product is not registered")
    transport = transport or Transport()
    spec = CATALOG[product]
    if provider == "official":
        if not spec["official_path"]:
            raise CollectionError("unsupported", "Official endpoint is not documented in the saved reference")
        # Header names come from the user's approved access instructions, not guesses.
        try:
            headers = json.loads(os.environ.get("IMD_OFFICIAL_HEADERS_JSON", "{}"))
            if not isinstance(headers, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in headers.items()):
                raise ValueError()
        except (ValueError, TypeError) as exc:
            raise CollectionError("configuration_error", "IMD_OFFICIAL_HEADERS_JSON must be a string-to-string object") from exc
        url = "https://api.imd.gov.in/api/v1/" + spec["official_path"]
        if params:
            url += "?" + urllib.parse.urlencode(params)
        payload = transport.json(url, headers=headers)
        if not isinstance(payload, (dict, list)) or (isinstance(payload, dict) and
                (payload.get("status") is False or "error" in payload)):
            raise CollectionError("upstream_error", "Official API returned an error or unexpected payload")
        result = Result(product, provider, "official", payload, {"compatibility": "upstream_passthrough"})
    elif provider == "public":
        if spec["public_provider"] == "astronomy_station":
            from .astronomy import collect_sunmoon
            result, payload = collect_sunmoon(params, transport)
            result.metadata.update({"parameters": params, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                                    "reference": "https://api.imd.gov.in/public/api_reference.html"})
            from .contracts import apply_documented_types
            apply_documented_types(result)
            return result, payload
        if spec["public_provider"] == "extended":
            from .extended_products import collect_extended
            result, payload = collect_extended(product, params, transport)
            result.metadata.update({"parameters": params, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                                    "reference": "https://api.imd.gov.in/public/api_reference.html"})
            from .contracts import apply_documented_types
            apply_documented_types(result)
            return result, payload
        if spec["public_provider"] == "feed":
            from .feeds import collect_feed
            result, payload = collect_feed(product, params, transport)
            result.metadata.update({"parameters": params, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                                    "reference": "https://api.imd.gov.in/public/api_reference.html"})
            from .contracts import apply_documented_types
            apply_documented_types(result)
            return result, payload
        if spec["public_provider"] in ("rainfall", "warnings"):
            from .products import collect_public
            result, payload = collect_public(product, params, transport)
            result.metadata.update({"parameters": params, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                                    "reference": "https://api.imd.gov.in/public/api_reference.html"})
            from .contracts import apply_documented_types
            apply_documented_types(result)
            return result, payload
        if spec["public_provider"] != "city":
            raise CollectionError("unsupported", "Public collector not implemented for this product yet")
        if not params:
            registry_url = PUBLIC_ROOT + "api/search.php?query="
            registry = transport.json(registry_url)
            stations = registry.get("data") if isinstance(registry, dict) else None
            if not isinstance(stations, list) or not stations:
                raise CollectionError("invalid_response", "Public city station registry missing")
            identities = list(dict.fromkeys(str(row.get("station_id", "")) for row in stations if isinstance(row, dict)))
            if not identities or not all(identity.isdigit() for identity in identities):
                raise CollectionError("invalid_response", "Invalid city station registry IDs")
            records, raw_records, failures, times = [], {}, [], []
            for identity in identities:
                try:
                    station, source = collect(product, {"id": identity}, "public", transport)
                    records.extend(station.data); raw_records[identity] = source
                    times.append({"identity": identity, "date": station.metadata.get("observation_date")})
                except CollectionError as exc:
                    failures.append({"id": identity, "status": exc.status})
            if not records:
                raise CollectionError("unavailable", "No city station weather records available")
            result = Result(product, "public", "partial" if failures or any(value is None for row in records for value in row.values()) else "mapped",
                            records, {"source_url": PUBLIC_ROOT + "api/fetchCity_static.php", "registry_url": registry_url,
                                      "parameters": {}, "retrieved_at": datetime.now(timezone.utc).isoformat(),
                                      "record_issue_times": times, "failed_stations": failures,
                                      "station_count_requested": len(identities),
                                      "missing_fields": {str(index): [key for key, value in row.items() if value is None]
                                                         for index, row in enumerate(records)}})
            from .contracts import apply_documented_types
            apply_documented_types(result)
            return result, {"registry": registry, "stations": raw_records}
        if set(params) != {"id"} or not str(params["id"]).isdigit():
            raise CollectionError("invalid_parameters", "Public city collection requires a numeric id only")
        url = PUBLIC_ROOT + "api/fetchCity_static.php"
        payload = transport.json(url, form={"ID": params["id"]}, headers={"Referer": PUBLIC_ROOT})
        result = map_city(payload, product, params["id"])
    else:
        raise CollectionError("configuration_error", "Provider must be public or official")
    result.metadata.update({"source_url": url.split("?")[0], "parameters": params,
                            "retrieved_at": datetime.now(timezone.utc).isoformat(),
                            "reference": "https://api.imd.gov.in/public/api_reference.html"})
    if provider == "public":
        from .contracts import apply_documented_types
        apply_documented_types(result)
    return result, payload


class Store:
    def __init__(self, path="data/imd.sqlite"):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with closing(sqlite3.connect(self.path)) as db:
            db.execute("CREATE TABLE IF NOT EXISTS snapshots (id INTEGER PRIMARY KEY, product TEXT, provider TEXT, params TEXT, retrieved_at TEXT, result TEXT, raw TEXT, raw_sha256 TEXT)")
            db.commit()

    @staticmethod
    def key(params):
        return json.dumps(params, sort_keys=True, separators=(",", ":"))

    def save(self, result, raw):
        encoded = json.dumps(raw, ensure_ascii=False, separators=(",", ":"))
        with closing(sqlite3.connect(self.path)) as db:
            db.execute("INSERT INTO snapshots (product,provider,params,retrieved_at,result,raw,raw_sha256) VALUES (?,?,?,?,?,?,?)",
                       (result.product, result.provider, self.key(result.metadata["parameters"]),
                        result.metadata["retrieved_at"], json.dumps(result.envelope()), encoded,
                        hashlib.sha256(encoded.encode()).hexdigest()))
            db.commit()

    def latest(self, product, provider, params):
        with closing(sqlite3.connect(self.path)) as db:
            row = db.execute("SELECT result FROM snapshots WHERE product=? AND provider=? AND params=? ORDER BY id DESC LIMIT 1",
                             (product, provider, self.key(params))).fetchone()
        return json.loads(row[0]) if row else None
