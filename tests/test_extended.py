import io
import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

from imd_local.artifacts import collect_artifact, documents, port_arrays
from imd_local.core import CollectionError, Store, Transport, Result
from imd_local.feeds import map_feed
from imd_local.operations import compare_shapes, freshness, load_jobs, run_jobs
from imd_local.cli import handler
from imd_local.core import CATALOG

FIXTURES = Path(__file__).parent / "fixtures"


def fixture(name):
    return json.loads((FIXTURES / name).read_text(encoding="utf-8-sig"))


class ExtendedTests(unittest.TestCase):
    def test_observations_map_exact_keys_and_do_not_invent_fields(self):
        for product, name in (("current_wx", "synop_data_layer.json"), ("aws_data", "aws_points.json"),
                              ("basinqpf", "indian_river_basin.json")):
            result = map_feed(product, fixture(name), {})
            self.assertEqual(set(result.data[0]), set(CATALOG[product]["documented_fields"]))
            if product == "aws_data":
                self.assertIsNone(result.data[0]["DISTRICT"])
                self.assertIsNone(result.data[0]["Feel Like"])
                self.assertNotIn("1970", result.data[0]["TIME"])
                self.assertIsNotNone(result.data[0]["Latitude"])
            elif product == "current_wx":
                self.assertIsNone(result.data[0]["Wind Speed"])
            else:
                self.assertEqual(result.data[0]["Day1"], "0.1 - 10 mm")

    def test_source_age_is_not_reset_by_fetch(self):
        now = datetime(2026, 10, 5, tzinfo=timezone.utc)
        result = {"metadata": {"retrieved_at": now.isoformat(),
                               "record_issue_times": [{"date": "2026-09-15", "updated_at": None}]}}
        self.assertTrue(freshness(result, now=now)["stale"])
        self.assertFalse(freshness(result, now=now)["retrieval_stale"])
        self.assertEqual(freshness(result, now=now)["source_stale_records"], 1)

    def test_unknown_source_time_is_explicit(self):
        value = freshness({"metadata": {"retrieved_at": datetime.now(timezone.utc).isoformat()}})
        self.assertEqual(value["source_time_status"], "unknown")

    def test_stale_api_refused_and_inspection_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Store(Path(directory) / "cache.sqlite")
            result = Result("cityforecast", "public", "mapped", [],
                            {"parameters": {}, "retrieved_at": (datetime.now(timezone.utc)-timedelta(days=4)).isoformat()})
            store.save(result, [])
            for route, expected in (("/api/v1/cityforecast", 503), ("/data/cityforecast", 200)):
                request = handler(store, "public").__new__(handler(store, "public"))
                request.path = route
                request.wfile = io.BytesIO()
                request.send_response = lambda code: setattr(request, "code", code)
                request.send_header = lambda *args: None
                request.end_headers = lambda: None
                request.do_GET()
                self.assertEqual(request.code, expected)

    def test_batch_continues_after_failure_and_reports_it(self):
        now = datetime.now(timezone.utc).isoformat()
        result = Result("cityforecast", "public", "mapped", [{}], {"parameters": {}, "retrieved_at": now})
        with tempfile.TemporaryDirectory() as directory, patch("imd_local.operations.collect") as collect:
            collect.side_effect = [CollectionError("requires_access", "HTTP 401"), (result, [{}])]
            reports = run_jobs([{"product": "sunmoon"}, {"product": "cityforecast"}], Store(Path(directory)/"db"), None)
            self.assertTrue(reports[0]["failed"])
            self.assertEqual(reports[1]["status"], "mapped")

    def test_no_retry_for_access_denial(self):
        transport = Transport(retries=2)
        with patch.object(transport, "_request", side_effect=CollectionError("requires_access", "HTTP 401")) as call:
            with self.assertRaises(CollectionError):
                transport.request("https://example.invalid")
            self.assertEqual(call.call_count, 1)

    def test_transient_retries_are_bounded(self):
        with patch("imd_local.core.time.sleep"), patch.object(Transport, "_request", side_effect=CollectionError("network_error", "failed")) as call:
            with self.assertRaises(CollectionError):
                Transport(retries=2).request("https://example.invalid")
            self.assertEqual(call.call_count, 3)

    def test_schema_comparison_checks_all_records_and_outer_shape(self):
        self.assertTrue(compare_shapes([{"x": 1}, {"x": "1"}], [{"x": 1}]))
        self.assertTrue(compare_shapes([{"x": 1}], {"data": [{"x": 1}]}))
        self.assertFalse(compare_shapes([{"x": 1}], [{"x": 2}]))

    def test_document_discovery_omits_navigation_and_footer(self):
        page = '<a href="nav.pdf">nav</a><div class="inner-page-content"><iframe src="product.pdf#toolbar=0"></iframe></div><section class="footer"><a href="footer.pdf">footer</a></section>'
        result = documents(page, "https://mausam.imd.gov.in/responsive/test.php")
        self.assertEqual([item["url"] for item in result], ["https://mausam.imd.gov.in/responsive/product.pdf"])
        self.assertFalse(result[0]["content_verified"])

    def test_port_parallel_arrays_have_matching_identity(self):
        text = fixture("artifact-port.json")
        rows = port_arrays(text)
        gopalpur = next(row for row in rows if row["name"] == "Gopalpur")
        self.assertEqual(gopalpur["latitude"], "19.2700")

    def test_mausamgram_uses_website_grid_and_rejects_errors(self):
        class Fake:
            def text(self, url): return "2026100400, 04-10-2026 10:53:51"
            def json(self, url):
                self.url = url
                return {"error": "No data found"}
        fake = Fake()
        with self.assertRaises(CollectionError):
            collect_artifact("mausamgram", {"lat": "19.27", "lon": "84.88"}, fake)
        self.assertIn("lat_gfs=19.250", fake.url)
        self.assertIn("lon_gfs=84.875", fake.url)

    def test_invalid_jobs_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"jobs.json"
            path.write_text('{"jobs":[{"product":"cityforecast","interval_seconds":0}]}')
            with self.assertRaises(CollectionError): load_jobs(path)


if __name__ == "__main__":
    unittest.main()
