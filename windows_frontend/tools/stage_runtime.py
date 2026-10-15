"""Assemble a Windows desktop folder without changing the shared services."""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHART_FILES = (
    "package.json",
    "utils/chartScale.js",
    "utils/grainChartRenderer.js",
    "tools/render_grain_charts.mjs",
)


def stage(app: Path, python: Path, node: Path, cpp: Path, web: Path,
          output: Path, native: Path | None = None, webview: Path | None = None) -> None:
    required = (app / "EBSDDesktop.exe", app / "resources.pri", python / "python.exe",
                node, cpp / "ebsd_cpp_backend.exe", web / "index.html")
    for path in required:
        if not path.is_file():
            raise ValueError(f"Missing required runtime file: {path}")
    if (python / "pyvenv.cfg").exists():
        raise ValueError("Use a relocatable Python distribution, not a copied virtualenv")
    for optional in (native, webview):
        if optional is not None and not optional.is_dir():
            raise ValueError(f"Runtime directory does not exist: {optional}")
    # Reject output inside an input tree before copytree can recursively copy itself.
    for source in (app, python, cpp, web, native, webview):
        if source is not None and output.resolve().is_relative_to(source.resolve()):
            raise ValueError("Output must be outside every source directory")
    if output.exists():
        raise ValueError(f"Output already exists; choose a fresh directory: {output}")

    shutil.copytree(app, output)
    shutil.copytree(web, output / "web")
    shutil.copytree(python, output / "runtime/python")
    (output / "runtime/node").mkdir(parents=True)
    shutil.copy2(node, output / "runtime/node/node.exe")
    shutil.copytree(cpp, output / "runtime/cpp")
    if native is not None:
        shutil.copytree(native, output / "runtime/native")
    else:
        (output / "runtime/native").mkdir(parents=True)
    if webview is not None:
        shutil.copytree(webview, output / "runtime/webview2")

    ml = output / "backend/ML_backend"
    ml.mkdir(parents=True)
    for source in (ROOT / "ML_backend").glob("*.py"):
        if not source.name.startswith("test_"):
            shutil.copy2(source, ml / source.name)
    shutil.copytree(ROOT / "ML_backend/assets", ml / "assets")
    for name in ("pyproject.toml", "uv.lock"):
        shutil.copy2(ROOT / "ML_backend" / name, ml / name)
    for name in CHART_FILES:
        destination = output / "backend/frontend" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "frontend" / name, destination)
    config = json.loads((ROOT / "windows_frontend/desktop.json").read_text(encoding="utf-8"))
    if webview is not None:
        config["webview_runtime"] = "runtime/webview2"
    (output / "desktop.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {output}; run EBSDDesktop.exe on Windows to verify the full report pipeline.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "python", "node", "cpp", "output"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--web", type=Path, default=ROOT / "frontend/.output/public")
    parser.add_argument("--native", type=Path, help="Directory with Cairo and dependent DLLs")
    parser.add_argument("--webview", type=Path, help="Optional fixed WebView2 runtime directory")
    stage(**vars(parser.parse_args()))


if __name__ == "__main__":
    main()
