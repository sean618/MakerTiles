#!/bin/bash
# Build the whole stack and run every test suite (C unit, C system, Python) at once.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build"

# Ensure the Python test deps are present (no-op if already installed).
if ! python3 -c "import pytest, serial" >/dev/null 2>&1; then
    echo ">> Installing Python deps (pytest, pyserial)..."
    pip install -r "$ROOT/daemon/requirements.txt" >/dev/null
fi

echo ">> Configuring..."
cmake -S "$ROOT" -B "$BUILD_DIR"

echo ">> Building (microbus, field protocol + shared lib, system test)..."
cmake --build "$BUILD_DIR" -j

echo ">> Running all tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
