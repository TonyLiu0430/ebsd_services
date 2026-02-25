vcpkg install --triplet=x64-linux-dynamic
cmake --preset default
cmake --build --preset default

./build/default/ebsd_cpp_backend 

套件:

sudo apt install -y \
  libx11-dev libxft-dev libxext-dev \
  libfontconfig1-dev libfreetype6-dev \
  libglib2.0-dev libpng-dev zlib1g-dev \
  pkg-config ninja-build