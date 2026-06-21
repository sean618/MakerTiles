cmake_minimum_required(VERSION 3.20)
project(systemtesting LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g")

# This test links the REAL microbus together with the field protocol
# (node + master + C daemon stand-in) to exercise the full stack in-process.
# Sources live in sibling component directories.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

file(GLOB TEST_SOURCES
        "${REPO_ROOT}/protocol/fieldprotocol/src/*.cpp"
        "${REPO_ROOT}/protocol/fieldprotocol/test/*.cpp"
        "${REPO_ROOT}/protocol/microbus/src/*.cpp")

# Exclude the python ctypes boundary. (STM32 HAL bindings live in protocol/stm32/
# and are never globbed here, so they need no exclusion.)
list(FILTER TEST_SOURCES EXCLUDE REGEX "fieldprotocol/test/pythonApi.cpp")

add_executable(system_tests ${TEST_SOURCES})
# Real microbus (no USE_MOCK_MICROBUS); C daemon stand-in enabled.
target_compile_definitions(system_tests PRIVATE USE_FP_DAEMON)

enable_testing()
# add_test(NAME system_integration COMMAND system_tests)
