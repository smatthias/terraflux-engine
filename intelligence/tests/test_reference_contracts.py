import json
import unittest
from pathlib import Path

from app.contracts import SpreadRequest
from app.spread_kernel import simulate


class ReferenceFixtureTest(unittest.TestCase):
    def test_reference_contract_fixtures_are_deterministic(self):
        path = Path(__file__).parent / "fixtures" / "grid_spread_cases.json"
        document = json.loads(path.read_text())
        self.assertEqual(document["contractVersion"], "geosentinel.compute/v1")
        for case in document["cases"]:
            with self.subTest(case=case["name"]):
                request = SpreadRequest.model_validate(case["request"])
                first = simulate(request).model_dump()
                second = simulate(request).model_dump()
                self.assertEqual(first, second)
                self.assertGreaterEqual(first["metrics"]["burned_cells"], 1)


if __name__ == "__main__":
    unittest.main()
