import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DOCKERFILE = REPOSITORY_ROOT / "ML_backend" / "Dockerfile"


class ChartRendererPackagingTests(unittest.TestCase):
    def test_dockerfile_chart_bundle_runs_without_repository_files(self) -> None:
        dockerfile = DOCKERFILE.read_text(encoding="utf-8")
        copied_frontend_files = re.findall(
            r"^COPY (frontend/\S+) (/app/frontend/\S+)$",
            dockerfile,
            flags=re.MULTILINE,
        )
        self.assertIn(
            ("frontend/utils/chartScale.js", "/app/frontend/utils/chartScale.js"),
            copied_frontend_files,
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            app_root = Path(temporary_directory) / "app"
            for source, destination in copied_frontend_files:
                target = app_root / destination.removeprefix("/app/")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / source, target)

            process = subprocess.run(
                ["node", str(app_root / "frontend/tools/render_grain_charts.mjs")],
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
