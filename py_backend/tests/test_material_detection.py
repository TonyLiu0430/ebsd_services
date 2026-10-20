import unittest

from app.services.ebsd import detect_material_from_metadata


class MaterialDetectionTests(unittest.TestCase):
    def test_detects_known_copper_fcc_lattice(self) -> None:
        result = detect_material_from_metadata({
            "phases": [{
                "space_group": 225,
                "lattice_constants": [3.615, 3.615, 3.615, 90, 90, 90],
            }]
        })

        self.assertEqual(result["material_key"], "Cu-FCC")
        self.assertEqual(result["confidence"], "high")
        self.assertEqual(result["matched_by"], ["lattice"])

    def test_unknown_lattice_remains_unknown(self) -> None:
        result = detect_material_from_metadata({
            "phases": [{
                "space_group": 1,
                "lattice_constants": [8, 9, 10, 70, 80, 100],
            }]
        })

        self.assertEqual(result["material_key"], "unknown")
        self.assertEqual(result["matched_by"], ["unknown"])
