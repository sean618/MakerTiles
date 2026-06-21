#!/bin/bash
#
# Builds both targets and runs the test suite WITHOUT requiring cmake, so it
# works in any environment that has a C++23 compiler. Set CXX to pick the
# compiler (defaults to c++); pass it as the first argument to test several:
#
#     ./build_and_test.sh            # uses $CXX or c++
#     CXX=g++ ./build_and_test.sh
#     ./build_and_test.sh g++ clang++
#
# Set FP_TEST_SEED=<n> to replay a specific random run.
#
# The build is standards-conformant C++23 and is verified under both g++ and
# clang++.

set -euo pipefail
cd "$(dirname "$0")"

# The field protocol itself is header-only (src/*.hpp); only the test/driver
# translation units below are compiled. The source lists are derived from the
# single manifest test/sources.txt (shared with CMakeLists.txt) so the two
# build paths cannot drift.
TEST_SRC=()
LIB_SRC=()
while read -r fname target || [ -n "$fname" ]; do
    case "$fname" in ''|\#*) continue ;; esac   # skip blanks and comments
    case "$target" in
        both) TEST_SRC+=("test/$fname"); LIB_SRC+=("test/$fname") ;;
        test) TEST_SRC+=("test/$fname") ;;
        lib)  LIB_SRC+=("test/$fname") ;;
        *) echo "sources.txt: unknown target '$target' for '$fname'" >&2; exit 1 ;;
    esac
done < test/sources.txt

COMPILERS=("$@")
if [ ${#COMPILERS[@]} -eq 0 ]; then
    COMPILERS=("${CXX:-c++}")
fi

mkdir -p build
STD="-std=c++23 -Wall -Isrc"

for CXX_BIN in "${COMPILERS[@]}"; do
    echo "=== Building with ${CXX_BIN} ==="

    echo "  - shared library (no daemon)"
    $CXX_BIN $STD -fPIC -shared -DUSE_MOCK_MICROBUS \
        "${LIB_SRC[@]}" -o "build/libmasterNodeSystem.so"

    echo "  - test executable"
    $CXX_BIN $STD -DUSE_FP_DAEMON -DUSE_MOCK_MICROBUS \
        "${TEST_SRC[@]}" -o "build/fieldprotocol_tests"

    echo "  - running tests"
    OUTPUT="$(build/fieldprotocol_tests)"
    echo "$OUTPUT" | tail -n 3
    if echo "$OUTPUT" | grep -q "^Passed$"; then
        echo "  ${CXX_BIN}: PASSED"
    else
        echo "  ${CXX_BIN}: FAILED (did not print 'Passed')"
        exit 1
    fi
    echo
done

echo "All builds passed."
