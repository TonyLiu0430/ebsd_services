vcpkg install --triplet=x64-linux-dynamic

cmake --preset default

cmake --build --preset default

./build/default/ebsd_cpp_backend 

toolchain:
sudo apt install -y gfortran