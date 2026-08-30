cmake_minimum_required(VERSION 3.25)

project(CH9344MacOSDriver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(ch9344_protocol STATIC
    src/protocol/EndpointLayout.cpp
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

add_executable(ch9344_endpoint_layout_tests
    tests/protocol/EndpointLayoutTests.cpp
)
target_link_libraries(ch9344_endpoint_layout_tests PRIVATE ch9344_protocol)
target_compile_options(ch9344_endpoint_layout_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME endpoint_layout COMMAND ch9344_endpoint_layout_tests)
set_tests_properties(endpoint_layout PROPERTIES LABELS protocol)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(ch9344_probe
    src/probe/LibusbDevice.cpp
    src/probe/main.cpp
)
set_target_properties(ch9344_probe PROPERTIES OUTPUT_NAME ch9344-probe)
target_link_libraries(ch9344_probe PRIVATE
    ch9344_protocol
    PkgConfig::LIBUSB
)
target_compile_options(ch9344_probe PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

option(CH9344_ENABLE_HARDWARE_TESTS "Enable tests that require a connected CH9344" OFF)
if(CH9344_ENABLE_HARDWARE_TESTS)
    add_test(
        NAME hardware_inspect
        COMMAND bash
            ${CMAKE_SOURCE_DIR}/tests/hardware/test_inspect.sh
            $<TARGET_FILE:ch9344_probe>
    )
    set_tests_properties(hardware_inspect PROPERTIES
        LABELS hardware
        RESOURCE_LOCK ch9344
    )

    add_test(
        NAME hardware_loopback_port4
        COMMAND bash
            ${CMAKE_SOURCE_DIR}/tests/hardware/test_loopback_port4.sh
            $<TARGET_FILE:ch9344_probe>
    )
    set_tests_properties(hardware_loopback_port4 PROPERTIES
        LABELS hardware
        RESOURCE_LOCK ch9344
    )
endif()
