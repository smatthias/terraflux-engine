import json
import unittest
from pathlib import Path

from app.contracts import CONTRACT_VERSION, SUPPORTED_CONTRACT_VERSIONS, SpreadRequest
from app.spread_kernel import simulate


class ReferenceFixtureTest(unittest.TestCase):
    def test_reference_contract_fixtures_are_deterministic(self):
        path = Path(__file__).parent / "fixtures" / "grid_spread_cases.json"
        document = json.loads(path.read_text())
        self.assertEqual(document["contractVersion"], CONTRACT_VERSION)
        for case in document["cases"]:
            with self.subTest(case=case["name"]):
                self.assertIn(case["request"]["contract_version"], SUPPORTED_CONTRACT_VERSIONS)
                request = SpreadRequest.model_validate(case["request"])
                first = simulate(request).model_dump()
                second = simulate(request).model_dump()
                self.assertEqual(first, second)
                self.assertEqual(first["contract_version"], case["request"]["contract_version"])
                if "expected_burned_cells" in case:
                    self.assertEqual(first["metrics"]["burned_cells"], case["expected_burned_cells"])
                else:
                    self.assertGreaterEqual(first["metrics"]["burned_cells"], 1)
                if "expected_burned" in case:
                    self.assertEqual([list(c) for c in first["frames"][-1]["burned_cells"]], case["expected_burned"])


if __name__ == "__main__":
    unittest.main()
