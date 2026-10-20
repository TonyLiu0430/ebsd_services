import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DOCKERFILE = REPOSITORY_ROOT / "py_backend" / "Dockerfile"


class ChartRendererPackagingTests(unittest.TestCase):
    def test_dockerfile_chart_bundle_runs_without_repository_files(self) -> None:
        dockerfile = DOCKERFILE.read_text(encoding="utf-8")
        self.assertIn("COPY shared/charts /app/shared/charts", dockerfile)
        self.assertNotIn("COPY frontend/", dockerfile)

        with tempfile.TemporaryDirectory() as temporary_directory:
            app_root = Path(temporary_directory) / "app"
            shutil.copytree(REPOSITORY_ROOT / "shared" / "charts", app_root / "shared" / "charts")

            process = subprocess.run(
                ["node", str(app_root / "shared/charts/bin/render-grain-charts.mjs")],
                input=json.dumps(
                    {
                        "charts": [
                            {
                                "sample": [5.0, 15.0, 25.0],
                                "golden": [7.0, 17.0, 27.0],
                                "mode": "hist",
                                "histYMode": "ratio",
                                "fixedXMin": 0,
                                "fixedXMax": 30,
                                "binWidth": 10,
                            }
                        ],
                        "sharedYMax": True,
                    }
                ),
                text=True,
                capture_output=True,
                check=False,
            )

        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result["sharedYMax"], 40)
        self.assertIn("<svg", result["charts"][0]["svg"])


if __name__ == "__main__":
    unittest.main()
