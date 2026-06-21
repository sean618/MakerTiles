#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

cmake -S. -Bbuild
cd build
cmake --build .
