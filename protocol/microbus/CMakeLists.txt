cmake_minimum_required(VERSION 3.28.3)
project(microbus)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_BUILD_TYPE Debug)

# set(EXECUTABLE_OUTPUT_PATH bin)

# Grab all source and test files - a bit messy but as this is a small project let's just keep it simple for now
FILE(GLOB TEST_SOURCES
        "src/*.cpp"
        "src/*.hpp"
        "test/*.cpp"
        "test/*.hpp")

list(FILTER TEST_SOURCES EXCLUDE REGEX "src/stm32F1SpiMaster.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "src/stm32G0SpiNode.cpp")

# Enable testing
enable_testing()

add_executable(microbus_tests ${TEST_SOURCES})

# Internal includes are protocol-relative ("microbus/src/...", "microbus/test/...").
# The parent of this dir (protocol/) is that root, whether microbus is built
# standalone or as a subdirectory of the top-level project.
target_include_directories(microbus_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Add tests
add_test(NAME microbus_unit COMMAND microbus_tests)
