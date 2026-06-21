cmake_minimum_required(VERSION 3.28.3)
project(fieldprotocol CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)  # standards-conformant: builds under both g++ and clang++

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable debug symbols
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Source lists come from the single manifest test/sources.txt (shared with
# build_and_test.sh) rather than file(GLOB), so adding a file updates both build
# paths in one place and CMake never needs a manual re-glob.
#   both -> compiled into the test exe AND the shared library
#   test -> test exe only
#   lib  -> shared library only
file(STRINGS test/sources.txt SRC_LINES)
set(TEST_SOURCES "")
set(LIB_SOURCES "")
foreach(line IN LISTS SRC_LINES)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    separate_arguments(parts UNIX_COMMAND "${line}")
    list(GET parts 0 fname)
    list(GET parts 1 target)
    if(target STREQUAL "both" OR target STREQUAL "test")
        list(APPEND TEST_SOURCES "test/${fname}")
    endif()
    if(target STREQUAL "both" OR target STREQUAL "lib")
        list(APPEND LIB_SOURCES "test/${fname}")
    endif()
endforeach()

add_library(masterNodeSystem SHARED ${LIB_SOURCES})

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS uses a different option for undefined symbols
    target_link_options(masterNodeSystem PRIVATE -Wl,-undefined,dynamic_lookup)
else()
    # Other systems (e.g., Linux) use --no-undefined
    target_link_options(masterNodeSystem PRIVATE -Wl,--no-undefined)
endif()

add_executable(fieldprotocol_tests ${TEST_SOURCES})

# USE_MOCK_MICROBUS applies to every target (the real microbus library is not
# vendored here). USE_FP_DAEMON is only defined for the test executable; the
# shared library hosts the Python daemon instead.
add_compile_definitions(USE_MOCK_MICROBUS)
target_compile_definitions(fieldprotocol_tests PRIVATE USE_FP_DAEMON USE_MOCK_MICROBUS)

# Enable testing
enable_testing()
add_test(NAME fieldprotocol_unit COMMAND fieldprotocol_tests)
