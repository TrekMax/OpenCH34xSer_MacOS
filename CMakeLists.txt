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

add_library(ch9344_driver_core STATIC
    Driver/Shared/DriverCore.cpp
    Driver/Shared/UsbTransaction.cpp
)
target_include_directories(ch9344_driver_core PUBLIC Driver/Shared)
target_link_libraries(ch9344_driver_core PUBLIC ch9344_protocol)
target_compile_options(ch9344_driver_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_executable(ch9344_driver_core_tests
    tests/driverkit/DriverCoreTests.cpp
)
target_link_libraries(ch9344_driver_core_tests PRIVATE ch9344_driver_core)
target_compile_options(ch9344_driver_core_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME driver_core COMMAND ch9344_driver_core_tests)
set_tests_properties(driver_core PROPERTIES LABELS driverkit)

add_executable(ch9344_usb_transaction_tests
    tests/driverkit/UsbTransactionTests.cpp
)
target_link_libraries(ch9344_usb_transaction_tests PRIVATE ch9344_driver_core)
target_compile_options(ch9344_usb_transaction_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME usb_transaction COMMAND ch9344_usb_transaction_tests)
set_tests_properties(usb_transaction PROPERTIES LABELS driverkit)

add_test(
    NAME driverkit_project_metadata
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/driverkit/test_project_metadata.sh
)
set_tests_properties(driverkit_project_metadata PROPERTIES LABELS driverkit)

add_test(
    NAME host_system_extension_controller
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/driverkit/test_host_controller.sh
)
set_tests_properties(host_system_extension_controller PROPERTIES LABELS driverkit)

add_test(
    NAME driverkit_unsigned_bundle
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/driverkit/test_unsigned_bundle.sh
)
set_tests_properties(driverkit_unsigned_bundle PROPERTIES LABELS driverkit)

option(
    CH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS
    "Enable tests that require an activated and entitled CH9344 DriverKit extension"
    OFF
)
if(CH9344_ENABLE_DRIVERKIT_SYSTEM_TESTS)
    add_test(
        NAME driverkit_activation
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/driverkit/test_activation.sh
    )
    add_test(
        NAME driverkit_serial_nodes
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/driverkit/test_serial_nodes.sh 1
    )
    add_test(
        NAME driverkit_port4_tty_loopback
        COMMAND python3
            ${CMAKE_SOURCE_DIR}/tests/driverkit/test_port4_tty_loopback.py
            /dev/cu.OpenCH34x4
    )
    set_tests_properties(
        driverkit_activation
        driverkit_serial_nodes
        driverkit_port4_tty_loopback
        PROPERTIES
            LABELS "driverkit;hardware"
            RESOURCE_LOCK ch9344
    )
endif()

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
