import copy
import json
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from imd_local.core import CATALOG, CollectionError, Store, collect, map_city
from imd_local.cli import handler

FIXTURE = json.loads((Path(__file__).parent / "fixtures/gopalpur-public.json").read_text(encoding="utf-8-sig"))


class FakeTransport:
    def __init__(self, payload):
        self.payload = payload
        self.calls = []

    def json(self, url, **kwargs):
        self.calls.append((url, kwargs))
        return self.payload


class CollectorTests(unittest.TestCase):
    def test_live_fixture_uses_exact_documented_keys(self):
        for product in ("cityforecast", "cityforecastloc"):
            result = map_city(FIXTURE, product, "43049")
            self.assertEqual(set(result.data[0]), set(CATALOG[product]["documented_fields"]))
            self.assertEqual(result.data[0]["Station_Code"], "43049")
            self.assertEqual(result.data[0]["Past_24_hrs_Rainfall"], "0.00")
            self.assertEqual(result.data[0]["Day_7_Min_temp"], "26.0")

    def test_observation_date_does_not_shift_forecast_dates(self):
        result = map_city(FIXTURE, "cityforecast", "43049")
        self.assertEqual(result.data[0]["Date"], "2026-10-04")
        self.assertEqual(result.metadata["forecast_dates_from_source"][0], "2026-10-05")
        self.assertIn("unverified", result.metadata["compatibility"])

    def test_missing_field_is_null_not_zero(self):
        payload = copy.deepcopy(FIXTURE)
        del payload[0]["rainfall"]
        result = map_city(payload, "cityforecast", "43049")
        self.assertIsNone(result.data[0]["Past_24_hrs_Rainfall"])
        self.assertEqual(result.status, "partial")

    def test_wrong_station_and_error_payload_rejected(self):
        for payload in ([{"status": False}], [{**FIXTURE[0], "station_id": "999"}]):
            with self.assertRaises(CollectionError):
                map_city(payload, "cityforecast", "43049")

    def test_official_payload_is_not_renamed_or_rewrapped(self):
        payload = {"status": True, "totalCount": 1, "data": [{"new_field": "001"}]}
        transport = FakeTransport(payload)
        with patch.dict("os.environ", {"IMD_OFFICIAL_HEADERS_JSON": '{"Authorization":"fixture"}'}):
            result, raw = collect("sunmoon", {"lat": "19.27", "lon": "84.88"}, "official", transport)
        self.assertEqual(result.data, payload)
        self.assertEqual(raw, payload)
        self.assertEqual(transport.calls[0][1]["headers"], {"Authorization": "fixture"})
        self.assertNotIn("fixture", json.dumps(result.envelope()))

    def test_upstream_error_is_not_success(self):
        with self.assertRaises(CollectionError):
            collect("cityforecast", {"id": "43049"}, "official", FakeTransport({"status": False}))

    def test_unimplemented_public_product_does_not_fall_back(self):
        transport = FakeTransport([])
        with self.assertRaises(CollectionError) as error:
            collect("cyclone_track", {}, "public", transport)
        self.assertEqual(error.exception.status, "unsupported")
        self.assertEqual(transport.calls, [])

    def test_cache_separates_query_and_provider(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory) / "test.sqlite")
            result, raw = collect("cityforecast", {"id": "43049"}, transport=FakeTransport(FIXTURE))
            store.save(result, raw)
            self.assertEqual(store.latest("cityforecast", "public", {"id": "43049"})["data"], result.data)
            self.assertIsNone(store.latest("cityforecast", "public", {"id": "123"}))
            self.assertIsNone(store.latest("cityforecast", "official", {"id": "43049"}))

    def request(self, store, path):
        # Fixed fixtures test response shape; separate tests verify source-age refusal.
        request_handler = handler(store, "public", max_source_age=10**12)
        request = request_handler.__new__(request_handler)
        request.path = path
        request.wfile = io.BytesIO()
        request.send_response = lambda code: setattr(request, "response_code", code)
        request.send_header = lambda *args: None
        request.end_headers = lambda: None
        request.do_GET()
        return request.response_code, json.loads(request.wfile.getvalue())

    def test_http_compatible_route_has_no_provenance_wrapper(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory) / "test.sqlite")
            result, raw = collect("cityforecast", {"id": "43049"}, transport=FakeTransport(FIXTURE))
            store.save(result, raw)
            code, body = self.request(store, "/api/v1/cityforecast?id=43049")
            self.assertEqual(code, 200)
            self.assertEqual(body, result.data)
            code, body = self.request(store, "/data/cityforecast?id=43049")
            self.assertEqual(body["data"], result.data)
            self.assertIn("retrieved_at", body["metadata"])

    def test_http_absence_is_not_empty_weather(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory) / "test.sqlite")
            code, body = self.request(store, "/api/v1/cityforecast?id=43049")
            self.assertEqual(code, 503)
            self.assertEqual(body["status"], "unavailable")
            code, body = self.request(store, "/api/v1/cyclone_track")
            self.assertEqual(code, 501)


if __name__ == "__main__":
    unittest.main()
