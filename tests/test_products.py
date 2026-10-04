import json
import unittest
from pathlib import Path

from imd_local.core import CATALOG, CollectionError, collect
from imd_local.products import category, map_warnings

FIXTURES = Path(__file__).parent / "fixtures"


class RainfallTransport:
    def text(self, url):
        kind = "state" if "_state.php" in url else "district"
        return (FIXTURES / f"rainfall-{kind}-{url[-1]}.html").read_text(encoding="utf-8-sig")


class ProductTests(unittest.TestCase):
    def test_rainfall_all_periods_and_documented_typos(self):
        for product, params in (("districtrainfall", {}), ("staterainfall", {"id": "odisha"})):
            result, raw = collect(product, params, transport=RainfallTransport())
            self.assertTrue(result.data)
            record = result.data if params else result.data[0]
            self.assertEqual(set(record), set(CATALOG[product]["documented_fields"]))
            self.assertIsNotNone(record["Weekly Actual"])
            self.assertIn("Monthly Category", result.metadata["derived_fields"])
        self.assertIsInstance(result.data, dict)
        self.assertIn("Monthly Acutual", result.data)

    def test_rainfall_invalid_filter_does_not_return_all(self):
        with self.assertRaises(CollectionError):
            collect("staterainfall", {"id": "nonexistent"}, transport=RainfallTransport())

    def test_rainfall_categories_boundary_and_zero_normal(self):
        self.assertEqual(category("0", "10", "-100%"), "NR")
        self.assertEqual(category("0.0", "5.5", "-99%"), "LD")  # displayed rainfall is rounded
        self.assertEqual(category("16", "10", "60%"), "LE")
        self.assertEqual(category("8.1", "10", "-19%"), "N")
        self.assertIsNone(category("0", "0", "0%"))
        self.assertIsNone(category(None, "10", "-100%"))

    def test_warning_all_documented_keys_and_colour_conventions(self):
        layers = {"districtwarning": "district_warnings_india", "districtnowcast": "NowcastWarningDistrict",
                  "stationnowcast": "NowcastWarningStation"}
        for product, layer in layers.items():
            raw = json.loads((FIXTURES / f"{layer}.json").read_text(encoding="utf-8-sig"))
            result = map_warnings(raw, product, {})
            self.assertEqual(len(result.data) + len(result.metadata["excluded_records"]), raw["numberReturned"])
            self.assertEqual(set(result.data[0]), set(CATALOG[product]["documented_fields"]))
            source = raw["features"][0]["properties"]
            key = "Day1_Color" if product == "districtwarning" else "color"
            self.assertEqual(result.data[0][key], source["Day1_Color" if product == "districtwarning" else "Color"])

    def test_empty_and_truncated_warning_feeds_rejected(self):
        for raw in ({"type": "FeatureCollection", "features": []},
                    {"type": "FeatureCollection", "features": [], "numberMatched": 5}):
            with self.assertRaises(CollectionError):
                map_warnings(raw, "districtwarning", {})
