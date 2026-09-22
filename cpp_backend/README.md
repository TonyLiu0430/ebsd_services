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

Run the native tests with:

```bash
cmake --build --preset default
ctest --test-dir build/default --output-on-failure
```
