import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("stage_runtime", Path(__file__).parents[1] / "tools/stage_runtime.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StagingTests(unittest.TestCase):
    def test_bundle_keeps_shared_renderer_and_runtime_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("app/EBSDDesktop.exe", "app/resources.pri", "python/python.exe",
                         "node.exe", "cpp/ebsd_cpp_backend.exe", "web/index.html"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            kwargs = {name: root / name for name in ("app", "python", "cpp", "web")}
            output = root / "bundle"
            module.stage(**kwargs, node=root / "node.exe", output=output)
            renderer = output / "backend/frontend/tools/render_grain_charts.mjs"
            result = subprocess.run(["node", str(renderer)], input=json.dumps({"charts": [
                {"sample": [5, 15, 25], "golden": [7, 17, 27], "mode": "hist", "histYMode": "ratio",
                 "fixedXMin": 0, "fixedXMax": 30, "binWidth": 10}], "sharedYMax": True}),
                text=True, capture_output=True, check=True)
            self.assertIn("<svg", json.loads(result.stdout)["charts"][0]["svg"])
            self.assertEqual(json.loads(result.stdout)["sharedYMax"], 40)
            config = json.loads((output / "desktop.json").read_text())
            for name in ("python_executable", "node_executable", "cpp_executable", "ml_root", "web_root"):
                self.assertTrue((output / config[name]).exists(), name)
            self.assertTrue((output / "backend/ML_backend/assets/ebsd_report_template.pptx").is_file())
            with self.assertRaisesRegex(ValueError, "already exists"):
                module.stage(**kwargs, node=root / "node.exe", output=output)
            with self.assertRaisesRegex(ValueError, "outside"):
                module.stage(**kwargs, node=root / "node.exe", output=root / "python/nested")
            (root / "python/pyvenv.cfg").touch()
            with self.assertRaisesRegex(ValueError, "virtualenv"):
                module.stage(**kwargs, node=root / "node.exe", output=root / "other")


if __name__ == "__main__":
    unittest.main()
