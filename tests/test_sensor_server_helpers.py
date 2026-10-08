import unittest

from sensor_server.config import Settings
from sensor_server.measurements import (
    canonicalize_sensor_names,
    flatten_numeric,
    infer_unit,
)
from sensor_server.security import token_digest, tokens_match


class MeasurementTests(unittest.TestCase):
    def test_legacy_tsl_names_are_canonicalized(self):
        sensors = {
            "tsl2584_1": {"visible_counts": 100},
            "tsl2584_2": {"visible_counts": 200},
            "sht45": {"temperature_c": 25.0},
        }

        canonical = canonicalize_sensor_names(sensors)

        self.assertNotIn("tsl2584_1", canonical)
        self.assertNotIn("tsl2584_2", canonical)
        self.assertEqual(canonical["tsl2584_sea"]["visible_counts"], 100)
        self.assertEqual(canonical["tsl2584_land"]["visible_counts"], 200)

    def test_canonical_tsl_name_wins_over_legacy_name(self):
        sensors = {
            "tsl2584_1": {"visible_counts": 100},
            "tsl2584_sea": {"visible_counts": 300},
        }

        canonical = canonicalize_sensor_names(sensors)

        self.assertEqual(canonical["tsl2584_sea"]["visible_counts"], 300)

    def test_nested_sensor_values_are_flattened(self):
        sensors = {
            "arduino_adc": {
                "ok": True,
                "channels": [
                    {"channel": 0, "raw_counts": 512, "voltage_v": 2.502},
                    {"channel": 1, "raw_counts": 200},
                ],
            }
        }
        values = list(flatten_numeric(sensors))
        self.assertIn(("arduino_adc.channels.0.raw_counts", 512.0, "count"), values)
        self.assertIn(("arduino_adc.channels.0.voltage_v", 2.502, "V"), values)
        self.assertNotIn(("arduino_adc.ok", 1.0, None), values)

    def test_non_finite_numbers_are_skipped(self):
        self.assertEqual(list(flatten_numeric({"bad": float("nan")})), [])

    def test_units(self):
        self.assertEqual(infer_unit("sht45.temperature_c"), "degC")
        self.assertEqual(infer_unit("rainfall_mm"), "mm")
        self.assertIsNone(infer_unit("sequence"))


class SecurityTests(unittest.TestCase):
    def test_token_helpers(self):
        self.assertEqual(len(token_digest("secret")), 64)
        self.assertTrue(tokens_match("secret", "secret"))
        self.assertFalse(tokens_match("secret", "different"))


class SettingsTests(unittest.TestCase):
    def test_invalid_device_token_json_is_rejected(self):
        import os
        from unittest.mock import patch

        with patch.dict(os.environ, {"DEVICE_TOKENS": "[]"}, clear=True):
            with self.assertRaises(RuntimeError):
                Settings.from_env()


if __name__ == "__main__":
    unittest.main()
