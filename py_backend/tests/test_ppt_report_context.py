import unittest
from types import SimpleNamespace
from unittest.mock import call, patch

from app.schemas import ReportPptRequest
from app.services.ebsd import report_ipf_vectors
from app.services.reports import build_ppt_report_context


class PptReportContextTests(unittest.TestCase):
    def test_microstructure_page_is_disabled_by_default(self) -> None:
        self.assertFalse(ReportPptRequest(sample="Cu-U").show_microstructure_page)

    def test_ppt_always_keeps_xyz_and_only_appends_a_custom_ipf_direction(self) -> None:
        self.assertEqual(list(report_ipf_vectors(None)), ["X", "Y", "Z"])
        self.assertEqual(list(report_ipf_vectors([1.0, 0.0, 0.0])), ["X", "Y", "Z"])

        custom = report_ipf_vectors([1.0, 1.0, 0.0])
        self.assertEqual(list(custom)[:3], ["X", "Y", "Z"])
        self.assertEqual(len(custom), 4)
        self.assertAlmostEqual(custom[next(reversed(custom))][0], 2 ** -0.5)
        self.assertAlmostEqual(custom[next(reversed(custom))][1], 2 ** -0.5)
        self.assertEqual(custom[next(reversed(custom))][2], 0.0)

    @patch("app.services.reports.cpp_pair_ipf_legend", return_value=b"legend")
    @patch("app.services.reports.cpp_inverse_pole_figure", return_value=b"inverse-pole")
    @patch("app.services.reports.cpp_pole_figure", return_value=b"pole")
    @patch("app.services.reports.cpp_band_contrast_map", return_value=b"band-contrast")
    @patch("app.services.reports.generate_python_scan_figures", return_value={})
    @patch("app.services.reports.cpp_ipf_map", return_value=b"ipf")
    @patch("app.services.reports.cpp_features")
    @patch("app.services.reports.collect_report_data")
    def test_shared_report_settings_are_forwarded_to_the_ppt_context(
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
            ReportPptRequest(
                sample="Cu-U",
                min_grain_size=7,
                ipf_reference_vector=[1.0, 0.0, 0.0],
                ipf_denoindex_threshold=85.0,
                ipf_denoindex_misorientation_threshold=12.0,
                hist_bin_start_mode="min",
                hist_bin_width_mode="ratio",
                hist_bin_width_ratio=7.5,
                hist_bin_width_absolute=12.5,
                grain_chart_mode="areaHist",
                histogram_y_mode="ratio",
                orientation_line_y_max=60.0,
                show_microstructure_page=True,
                show_triangle_labels=False,
                show_orientation_line_labels=False,
                selected_features=["grain size average"],
                color_thresholds=[20.0, 80.0],
            ),
            request=object(),
            db=object(),
        )

        self.assertEqual(context["statistics_by_min_grain_size"], {7: selected_snapshot})
        self.assertEqual(
            context["histogram_settings"],
            {
                "start_mode": "min",
                "width_mode": "ratio",
                "width_ratio": 7.5,
                "width_absolute": 12.5,
                "chart_mode": "areaHist",
                "y_mode": "ratio",
            },
        )
        self.assertEqual(context["orientation_line_y_max"], 60.0)
        self.assertTrue(context["show_microstructure_page"])
        self.assertFalse(context["show_triangle_labels"])
        self.assertFalse(context["show_orientation_line_labels"])
        self.assertEqual(context["analysis_features"], ["grain size average"])
        self.assertEqual(context["color_thresholds"], [20.0, 80.0])
        self.assertEqual(list(context["ipf_images_by_direction"]), ["X", "Y", "Z"])
        self.assertEqual(
            list(context["scan_report_images"]["A-1"]["ipf_maps"]),
            ["X", "Y", "Z"],
        )
        self.assertEqual(
            _cpp_ipf_map.call_args_list,
            [
                call(row, [1.0, 0.0, 0.0], 85.0, 12.0),
                call(row, [0.0, 1.0, 0.0], 85.0, 12.0),
                call(row, [0.0, 0.0, 1.0], 85.0, 12.0),
            ],
        )
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
