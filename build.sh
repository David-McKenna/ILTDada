rm -rf ./build/
mkdir build
cd build
cmake ..
cmake --build .
sudo cmake --install .