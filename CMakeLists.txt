cmake_minimum_required(VERSION 3.20)
project(makertiles_system LANGUAGES C)

# One build tree, one test runner for the whole stack:
#   microbus            -> microbus_unit          (unit tests, host)
#   FieldProtocol       -> fieldprotocol_unit      (unit tests, mock microbus)
#                          + masterNodeSystem.so   (loaded by the Python daemon)
#   tests/system        -> system_integration      (full C stack, real microbus)
#   daemon              -> python_unit             (protocol encode/decode)
#                          python_integration       (daemon driving the C lib)
enable_testing()

# Internal includes are written relative to the protocol/ dir, e.g.
# "microbus/src/config.hpp", so protocol/ is the include search root for every
# subproject below (the repo root is also added for any root-relative paths).
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/protocol)

add_subdirectory(protocol/microbus)
add_subdirectory(protocol/fieldprotocol)
add_subdirectory(tests/system)

# ---------------------------------------------------------------------------
# Python daemon tests
# ---------------------------------------------------------------------------
find_program(PYTEST_EXECUTABLE NAMES pytest pytest-3)
set(DAEMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/daemon)

if(PYTEST_EXECUTABLE)
    # Pure-Python: field-protocol encode/decode round trips. No native lib.
    add_test(NAME python_unit
             COMMAND ${PYTEST_EXECUTABLE} -q tests/test_field_protocol.py
             WORKING_DIRECTORY ${DAEMON_DIR})

    # Integration: the real Python daemon talks to the C field protocol through
    # the masterNodeSystem shared library (mock microbus transport). This is the
    # test that actually catches C<->Python wire-contract drift.
    add_test(NAME python_integration
             COMMAND ${PYTEST_EXECUTABLE} -q tests/test_main.py
             WORKING_DIRECTORY ${DAEMON_DIR})
    # Hand the test the exact path of the lib we just built, wherever it landed.
    set_tests_properties(python_integration PROPERTIES
        ENVIRONMENT "MASTER_NODE_SYSTEM_LIB=$<TARGET_FILE:masterNodeSystem>")
else()
    message(WARNING "pytest not found - Python tests will be skipped. Install with: pip install pytest")
endif()
