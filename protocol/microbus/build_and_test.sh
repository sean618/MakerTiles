#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e
cmake -DCMAKE_C_COMPILER=clang -S. -Bbuild
cd build
cmake --build .
ctest
