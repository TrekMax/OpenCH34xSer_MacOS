cmake_minimum_required(VERSION 3.25)

project(CH9344MacOSDriver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(ch9344_protocol STATIC
    src/protocol/Protocol.cpp
)
target_include_directories(ch9344_protocol PUBLIC include)
target_compile_options(ch9344_protocol PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_executable(ch9344_protocol_tests
    tests/protocol/ProtocolTests.cpp
)
target_link_libraries(ch9344_protocol_tests PRIVATE ch9344_protocol)
target_compile_options(ch9344_protocol_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME protocol COMMAND ch9344_protocol_tests)
set_tests_properties(protocol PROPERTIES LABELS protocol)
