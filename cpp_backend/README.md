`linux-dynamic` auto-detects `x64`/`arm64` from `uname -m` and forces dynamic `vcpkg` linkage.

vcpkg install --overlay-triplets=./vcpkg-triplets --triplet=linux-dynamic

cmake --preset default

cmake --build --preset default

cmake --preset release

cmake --build --preset release

toolchain:
sudo apt install -y gfortran

## Band contrast map API

`POST /band_contrast_map` returns the uploaded Oxford EBSD band contrast data as a grayscale PNG. It accepts either:

- `ebsd`: a `.ctf` file
- `ebsd` (or `cpr`): a `.cpr` file, together with its matching `.crc` in the `crc` field

The optional `include_scale_bar` form field accepts `true` or `false` and defaults to `true`. ANG files are rejected because they contain Image Quality rather than Band Contrast.

```bash
curl -X POST http://localhost:8080/band_contrast_map \
  -F 'ebsd=@sample.ctf' \
  -F 'include_scale_bar=false' \
  --output band-contrast.png
```

## Pole-figure APIs

`POST /pole_figure` and `POST /inverse_pole_figure` generate the report PF and
IPF texture plots in C++. They accept the same `.cpr` + `.crc`, `.ctf`, or
`.ang` multipart uploads as the other EBSD endpoints. The renderer preserves
the report's Channel 5 coordinate convention, equal-area projections, 10°
smoothing bandwidth, common three-panel MRD scale, cubic/HCP phase handling,
and fixed 1260×423 PNG size.

```bash
curl -X POST http://localhost:8080/pole_figure \
  -F 'cpr=@sample.cpr' -F 'crc=@sample.crc' \
  --output pole-figure.png

curl -X POST http://localhost:8080/inverse_pole_figure \
  -F 'cpr=@sample.cpr' -F 'crc=@sample.crc' \
  --output inverse-pole-figure.png
```

Run the native tests and the Python-to-C++ random-data parity suite with:

```bash
cmake --build --preset default
ctest --test-dir build/default --output-on-failure

cd ../ML_backend
EBSD_TEXTURE_FIXTURE=../cpp_backend/build/default/texture_parity_fixture \
  .venv/bin/python -m unittest -v test_texture_cpp_parity
```

The parity suite uses two deterministic cubic random Euler-angle sets plus an
HCP case. It compares all six cubic 251×251 density panels, all three HCP PF
density panels, and decodes every final PF/IPF PNG to check whole-image error,
coloured-area coverage, and colour-distribution agreement against the previous
Python implementation.

For an end-to-end comparison using the real `.cpr`/`.crc` scans under `.temp`,
including strict foreground-only image metrics (so white margins cannot hide a
bad plot), run:

```bash
cd ../ML_backend
MPLCONFIGDIR=/tmp/mpl-cache .venv/bin/python \
  tools/compare_texture_figures.py ../.temp
```

The command writes paired Python/C++ PNGs, every 251×251 density grid, and a
machine-readable summary to `.temp/texture-comparison/report.json`. A scan only
passes when every density panel has correlation ≥ 0.9999 and normalized MAE ≤
0.001, and both final images satisfy foreground MAE ≤ 0.05, foreground IoU ≥
0.97, and foreground RGB correlation ≥ 0.985.
