import unittest

from app.analysis.service import compute_analysis


class AnalysisTests(unittest.TestCase):
    def test_compute_analysis_averages_feature_results(self) -> None:
        result = compute_analysis(
            ["mean"],
            "Golden",
            {
                "Golden": {"A-1": {"grains": [10.0, 20.0]}},
                "Sample": {"A-1": {"grains": [15.0, 25.0]}},
            },
        )

        self.assertAlmostEqual(result["Sample"]["A-1"], 1.0 / 3.0)


if __name__ == "__main__":
    unittest.main()
