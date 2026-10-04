import io
import json
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
from pathlib import Path

from imd_local.core import CATALOG, CollectionError, Result
from imd_local.contracts import apply_documented_types, validate_example
from imd_local.extended_products import map_subdivision, map_district_forecast, map_bulletin, map_ports
from imd_local.document_content import document_content
from imd_local.geography import polygon_contains, assign_states, state_matches

FIXTURES = Path(__file__).parent / "fixtures"
ROOT = Path(__file__).resolve().parents[1]


def load(name):
    return json.loads((FIXTURES / name).read_text(encoding="utf-8-sig"))


class ReferenceTests(unittest.TestCase):
    def test_subdivisions_match_reference_samples(self):
        for product in ("subdivisionwarning", "subdivision_rainfall_forecast"):
            rows, _ = map_subdivision(product, load("extended-" + product + ".json"))
            result = Result(product, "public", "mapped", rows)
            apply_documented_types(result)
            self.assertEqual(len(rows), 36)
            self.assertEqual(validate_example(rows, CATALOG[product]["documented_example"]), [])

    def test_unknown_warning_is_null_not_no_warning(self):
        payload = load("extended-subdivisionwarning.json")
        payload["features"][0]["properties"]["Day_1"] = "999"
        rows, _ = map_subdivision("subdivisionwarning", payload)
        self.assertIsNone(rows[0]["day1_warning"])

    def test_forecast_dates_advance_and_states_join_by_object_id(self):
        payload = load("extended-state_district_rainfall_forecast.json")
        rows, times, inconsistent = map_district_forecast(payload["pages"], load("NowcastWarningDistrict.json"))
        self.assertEqual(len(rows), 751)
        self.assertFalse(inconsistent)
        self.assertTrue(all(row["State"] for row in rows))
        row = next(row for row in rows if row["Obj_id"] == "573")
        self.assertEqual(row["day1_distribution"], "Scattered")
        self.assertEqual(row["day5_distribution"], "Isolated")
        self.assertEqual(validate_example(rows, CATALOG["state_district_rainfall_forecast"]["documented_example"]), [])
        time = next(time for time in times if time["identity"] == "573")
        self.assertNotEqual(time["forecast_dates"]["1"], time["forecast_dates"]["5"])

    def test_marine_missing_identifiers_are_not_invented(self):
        for filename, product, count in (("sea-bay", "seabulletin", 8), ("coastal-kolkata", "coastalbulletin", 2)):
            rows, times = map_bulletin((ROOT / "references" / (filename + ".html")).read_text(encoding="utf-8"), product)
            self.assertEqual(len(rows), count)
            self.assertIsNone(rows[0]["Id"])
            self.assertIsNone(rows[0]["Date of Observation"])
            self.assertEqual(rows[0]["Valid From"], "2026-10-04 21:00:00")
            self.assertEqual(rows[0]["Issued by"], "ACWC KOLKATA")
            self.assertEqual(validate_example(rows, CATALOG[product]["documented_example"]), [])
            self.assertTrue(times[0]["updated_at"].endswith("+05:30"))

    def test_port_ids_dates_and_exact_documented_keys(self):
        rows, _ = map_ports((ROOT / "references/source-10.html").read_text(encoding="utf-8"))
        self.assertEqual(len(rows), len({row["Port Id"] for row in rows}))
        self.assertEqual(set(rows[0]), set(CATALOG["portwarning"]["documented_fields"]))
        self.assertEqual(rows[0]["Date of Issue"], "2026-10-04")

    def test_document_content_is_verified_and_stored_by_hash(self):
        from pypdf import PdfWriter
        writer = PdfWriter(); writer.add_blank_page(width=100, height=100)
        stream = io.BytesIO(); writer.write(stream)
        class Fake:
            def request(self, *_args, **_kwargs): return stream.getvalue()
        with tempfile.TemporaryDirectory() as directory:
            doc = document_content({"url": "https://mausam.imd.gov.in/a.pdf", "kind": "pdf"}, Fake(), directory)
            self.assertTrue(doc["content_verified"])
            self.assertEqual(doc["text_status"], "image_only")
            self.assertEqual(Path(doc["file"]).name, doc["sha256"] + ".pdf")
            self.assertEqual(Path(doc["file"]).read_bytes(), stream.getvalue())

    def test_document_html_error_not_saved_as_pdf(self):
        class Fake:
            def request(self, *_args, **_kwargs): return b'<html>Forbidden</html>'
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(CollectionError):
            document_content({"url": "https://mausam.imd.gov.in/a.pdf", "kind": "pdf"}, Fake(), directory)

    def test_types_come_from_documented_example(self):
        value = {key: None for key in CATALOG["aws_data"]["documented_fields"]}
        value["CURR_TEMP"] = 23.5
        result = Result("aws_data", "public", "partial", [value])
        apply_documented_types(result)
        self.assertIsInstance(result.data[0]["CURR_TEMP"], str)

    def test_polygon_holes_and_overlaps_do_not_invent_a_state(self):
        polygon = [[[0,0],[10,0],[10,10],[0,10],[0,0]], [[4,4],[6,4],[6,6],[4,6],[4,4]]]
        self.assertTrue(polygon_contains(2,2,polygon))
        self.assertFalse(polygon_contains(5,5,polygon))
        payload = {"type":"FeatureCollection", "features":[
            {"properties":{"stname": name}, "geometry":{"type":"Polygon", "coordinates":polygon}}
            for name in ["FIRST", "SECOND"]]}
        rows = [{"Latitude":2,"Longitude":2,"STATE":None}]
        assign_states(rows,payload)
        self.assertIsNone(rows[0]["STATE"])
        self.assertTrue(state_matches("NCT OF DELHI", "DELHI"))

    def test_aws_documented_id_is_call_sign(self):
        from imd_local.feeds import map_feed
        payload = load("aws_points.json")
        rows = map_feed("aws_data", payload, {"id":"NCL"}).data
        self.assertTrue(rows)
        self.assertTrue(all(row["CALL_SIGN"] == "NCL" for row in rows))

    def test_city_collection_without_id_uses_registry_and_isolates_failure(self):
        from imd_local.core import collect
        city = load("gopalpur-public.json")
        class Fake:
            def json(self, url, **kwargs):
                if "search.php" in url:
                    return {"data":[{"station_id":"43049"},{"station_id":"12345"}]}
                if kwargs["form"]["ID"] == "12345":
                    raise CollectionError("unavailable", "Missing station")
                return city
        result, raw = collect("cityforecast", {}, transport=Fake())
        self.assertEqual(len(result.data),1)
        self.assertEqual(result.status,"partial")
        self.assertEqual(result.metadata["station_count_requested"],2)
        self.assertEqual(result.metadata["failed_stations"][0]["id"],"12345")

    def test_astronomy_matches_documented_wrapper_without_nearest_station(self):
        from imd_local.astronomy import collect_sunmoon
        payload = load("sunmoon-public.json")
        class Fake:
            def json(self, url, **kwargs):
                return payload["registry"] if "GetFeature" in url else payload["city"]
        result, _ = collect_sunmoon({"lat":"19.27","lon":"84.88"},Fake())
        self.assertEqual(validate_example(result.data,CATALOG["sunmoon"]["documented_example"]),[])
        self.assertIsNone(result.metadata["astronomy_validity_date"])
        self.assertEqual(result.data["data"][0]["sunrise"],"05:39")
        with self.assertRaises(CollectionError):
            collect_sunmoon({"lat":"19.28","lon":"84.88"},Fake())

    def test_artifact_and_reference_validation_cli_paths(self):
        from imd_local.cli import main
        from datetime import datetime, timezone
        artifact = Result("artifact:national","public","source_native",{"documents":[]},
                          {"parameters":{},"retrieved_at":datetime.now(timezone.utc).isoformat()})
        with tempfile.TemporaryDirectory() as directory, redirect_stdout(io.StringIO()), patch("imd_local.cli.collect_artifact",return_value=(artifact,{})):
            self.assertEqual(main(["--database",str(Path(directory)/"db.sqlite"),"artifact","national","--index-only"]),0)
            payload = Path(directory)/"payload.json"
            payload.write_text(json.dumps(CATALOG["sunmoon"]["documented_example"]),encoding="utf-8")
            self.assertEqual(main(["validate","sunmoon",str(payload)]),0)

    def test_single_filtered_record_uses_documented_object_shape(self):
        row = {key:None for key in CATALOG["aws_data"]["documented_fields"]}
        result = Result("aws_data","public","partial",[row],{"parameters":{"id":"NDL"}})
        apply_documented_types(result)
        self.assertIsInstance(result.data,dict)
        self.assertEqual(validate_example(result.data,CATALOG["aws_data"]["documented_example"]),[])


if __name__ == "__main__": unittest.main()
