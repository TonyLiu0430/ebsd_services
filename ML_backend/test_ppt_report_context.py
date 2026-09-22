import unittest
from types import SimpleNamespace
from unittest.mock import patch

from main import ReportPptRequest, build_ppt_report_context


class PptReportContextTests(unittest.TestCase):
    @patch("main.cpp_pair_ipf_legend", return_value=b"legend")
    @patch("main.cpp_inverse_pole_figure", return_value=b"inverse-pole")
    @patch("main.cpp_pole_figure", return_value=b"pole")
    @patch("main.cpp_band_contrast_map", return_value=b"band-contrast")
    @patch("main.generate_python_scan_figures", return_value={})
    @patch("main.cpp_ipf_map", return_value=b"ipf")
    @patch("main.cpp_features")
    @patch("main.collect_report_data")
    def test_statistics_only_use_requested_min_grain_size(
        self,
        collect_report_data,
        cpp_features,
        _cpp_ipf_map,
        _generate_python_scan_figures,
        _cpp_band_contrast_map,
        _cpp_pole_figure,
        _cpp_inverse_pole_figure,
        _cpp_pair_ipf_legend,
    ) -> None:
        row = SimpleNamespace(
            pair=SimpleNamespace(
                crc_file=SimpleNamespace(content=b"crc"),
                cpr_file=SimpleNamespace(content=b"cpr"),
            )
        )
        selected_snapshot = {"A-1": {"grains": [3.0, 4.0]}}
        collect_report_data.return_value = {
            "selected_sample": "Cu-U",
            "golden_sample": "",
            "selected_version_key": "v1",
            "golden_version_key": "",
            "selected_version_label": "Cu-U-1",
            "golden_version_label": "",
            "versioned_report_data": {"Cu-U": {"v1": selected_snapshot}},
            "cumulative_pairs": {"Cu-U": {"v1": {"A-1": row}}},
            "selected_overridden_positions": [],
        }

        context = build_ppt_report_context(
            ReportPptRequest(sample="Cu-U", min_grain_size=7),
            request=object(),
            db=object(),
        )

        self.assertEqual(context["statistics_by_min_grain_size"], {7: selected_snapshot})
        cpp_features.assert_not_called()
        _cpp_pole_figure.assert_called_once_with(row)
        _cpp_inverse_pole_figure.assert_called_once_with(row)
        self.assertEqual(context["scan_report_images"]["A-1"]["pole_figure"], b"pole")
        self.assertEqual(
            context["scan_report_images"]["A-1"]["inverse_pole_figure"],
            b"inverse-pole",
        )


if __name__ == "__main__":
    unittest.main()
