import json
import unittest
from pathlib import Path

from engine_bridge import CAPTURE_VIEWS, EngineBridgeError, normalize_capture_path, validate_capture_view


class EngineBridgeContractTest(unittest.TestCase):
    def test_catalog_is_valid_json(self) -> None:
        catalog_path = Path(__file__).with_name("command_catalog.json")
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        self.assertEqual(catalog["catalog_version"], 1)
        self.assertEqual(catalog["engine_transport"]["default_port"], 37373)

    def test_capture_views_use_engine_names(self) -> None:
        self.assertIn("world_normal", CAPTURE_VIEWS)
        self.assertIn("linear_depth", CAPTURE_VIEWS)
        self.assertEqual(validate_capture_view("world_normal"), "world_normal")
        self.assertEqual(validate_capture_view("linear_depth"), "linear_depth")

    def test_capture_view_aliases_are_rejected(self) -> None:
        with self.assertRaises(EngineBridgeError):
            validate_capture_view("normal")
        with self.assertRaises(EngineBridgeError):
            validate_capture_view("depth")

    def test_capture_path_is_confined_to_validation_directory(self) -> None:
        self.assertEqual(
            normalize_capture_path("save/screenshots/validation/test.png"),
            "save/screenshots/validation/test.png",
        )
        with self.assertRaises(EngineBridgeError):
            normalize_capture_path("save/screenshots/validation/test.jpg")
        with self.assertRaises(EngineBridgeError):
            normalize_capture_path("save/../outside.png")


if __name__ == "__main__":
    unittest.main()
