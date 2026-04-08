`linux-dynamic` auto-detects `x64`/`arm64` from `uname -m` and forces dynamic `vcpkg` linkage.

vcpkg install --overlay-triplets=./vcpkg-triplets --triplet=linux-dynamic

cmake --preset default

cmake --build --preset default

cmake --preset release

cmake --build --preset release

toolchain:
sudo apt install -y gfortran
