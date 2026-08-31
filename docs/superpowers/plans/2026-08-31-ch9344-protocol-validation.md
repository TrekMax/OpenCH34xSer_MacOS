# CH9344 Protocol Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tested, platform-neutral CH9344 protocol core and a macOS libusb probe that proves the fourth physical UART can complete a TX/RX loopback.

**Architecture:** A C++17 static library owns byte-level framing and command encoding without USB or macOS dependencies. A small libusb command-line adapter discovers the real endpoints and performs hardware I/O, while CTest separates always-on protocol tests from explicitly enabled hardware tests.

**Tech Stack:** C++17, CMake 3.25+, CTest, Apple Clang, libusb-1.0, zsh/bash hardware scripts.

## Global Constraints

- Support only VID `0x1a86`, PID `0xe018`, interface `0`, and four CH9344 ports.
- User-facing ports are `1...4`; protocol logical ports are `0...3`; hardware port IDs are `4...7`.
- The fourth physical port is the only port with TX/RX shorted during this plan.
- Support baud rate, 8N1, RX, TX, DTR, and RTS; do not add GPIO, CH348, RS-485, or hardware flow control.
- Follow strict RED, GREEN, REFACTOR: observe the expected failing test before production implementation.
- Keep every committed revision buildable and green; record RED evidence instead of committing a broken tree.
- Use Chinese Conventional Commit subjects, stage only explicit paths, and do not push.
- Do not copy GPL implementation code from the Linux driver; implement the documented byte behavior independently.

---

## File Structure

- `CMakeLists.txt`: protocol library, test targets, probe target, and opt-in hardware tests.
- `include/ch9344/Protocol.hpp`: stable platform-neutral protocol types and function declarations.
- `src/protocol/Protocol.cpp`: byte-level CH9344 implementation.
- `tests/TestSupport.hpp`: small assertion helpers shared by standalone test executables.
- `tests/protocol/ProtocolTests.cpp`: literal-vector unit tests for all protocol behavior.
- `include/ch9344/EndpointLayout.hpp`: platform-neutral USB endpoint descriptor abstraction.
- `src/protocol/EndpointLayout.cpp`: validates and classifies the four expected Bulk endpoints.
- `tests/protocol/EndpointLayoutTests.cpp`: endpoint-order and malformed-layout tests.
- `src/probe/LibusbDevice.hpp`: RAII boundary for the external USB device.
- `src/probe/LibusbDevice.cpp`: libusb discovery, claim, control, Bulk transfer, and cleanup.
- `src/probe/main.cpp`: `inspect` and `loopback` command parsing and stable console output.
- `tests/hardware/test_inspect.sh`: real-device descriptor/version integration test.
- `tests/hardware/test_loopback_port4.sh`: real fourth-port TX/RX integration test.
- `docs/verification/tdd/2026-08-31-protocol-validation.md`: RED/GREEN commands and observed outcomes.
- `docs/verification/2026-08-31-port4-loopback.md`: final hardware evidence and limits.

### Task 1: Build scaffold and logical-port mapping

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/ch9344/Protocol.hpp`
- Create: `src/protocol/Protocol.cpp`
- Create: `tests/TestSupport.hpp`
- Create: `tests/protocol/ProtocolTests.cpp`
- Create: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `ch9344::Error mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort)`.
- Produces: CMake target `ch9344_protocol_tests` and CTest test `protocol`.

- [ ] **Step 1: Create the non-behavioral build scaffold**

Create a C++17 static library and one test executable with strict warnings:

```cmake
cmake_minimum_required(VERSION 3.25)
project(CH9344MacOSDriver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

add_library(ch9344_protocol STATIC src/protocol/Protocol.cpp)
target_include_directories(ch9344_protocol PUBLIC include)
target_compile_options(ch9344_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(ch9344_protocol_tests tests/protocol/ProtocolTests.cpp)
target_link_libraries(ch9344_protocol_tests PRIVATE ch9344_protocol)
target_compile_options(ch9344_protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME protocol COMMAND ch9344_protocol_tests)
set_tests_properties(protocol PROPERTIES LABELS protocol)
```

`src/protocol/Protocol.cpp` initially contains only `#include <ch9344/Protocol.hpp>`.

- [ ] **Step 2: Write the failing mapping test**

Declare the API but do not implement it. The test uses literal expectations so changing the base port or accepting port 4 breaks it:

```cpp
namespace ch9344 {
enum class Error { none, invalidArgument, invalidPort };
Error mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort);
}

void testLogicalPortMapping()
{
    const uint8_t expected[] = {4, 5, 6, 7};
    for (uint8_t logical = 0; logical < 4; ++logical) {
        uint8_t hardware = 0;
        CHECK_EQ(ch9344::mapLogicalPort(logical, &hardware), ch9344::Error::none);
        CHECK_EQ(hardware, expected[logical]);
    }

    uint8_t untouched = 0xa5;
    CHECK_EQ(ch9344::mapLogicalPort(4, &untouched), ch9344::Error::invalidPort);
    CHECK_EQ(untouched, 0xa5);
    CHECK_EQ(ch9344::mapLogicalPort(0, nullptr), ch9344::Error::invalidArgument);
}
```

`tests/TestSupport.hpp` defines `CHECK_EQ` and `CHECK_BYTES` to print file, line, and source expressions; each test executable owns its failure counter and returns nonzero when any check fails.

- [ ] **Step 3: Run RED and record the expected failure**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target ch9344_protocol_tests
```

Expected: link failure for undefined `ch9344::mapLogicalPort`; the failure must not be a missing include or test syntax error. Record command, exit code, and undefined-symbol reason in the TDD evidence document.

- [ ] **Step 4: Implement the minimum mapping behavior**

```cpp
ch9344::Error ch9344::mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort)
{
    if (hardwarePort == nullptr) {
        return Error::invalidArgument;
    }
    if (logicalPort >= 4) {
        return Error::invalidPort;
    }
    *hardwarePort = static_cast<uint8_t>(logicalPort + 4);
    return Error::none;
}
```

- [ ] **Step 5: Run GREEN and commit**

Run:

```bash
cmake --build build --target ch9344_protocol_tests
ctest --test-dir build --output-on-failure
git diff --check
```

Expected: one test passes, zero failures, and no warnings. Record GREEN evidence, then commit only the six Task 1 paths:

```bash
git commit -m "feat: 实现 CH9344 端口映射"
```

### Task 2: TX frame encoding and bounded fragmentation

**Files:**
- Modify: `include/ch9344/Protocol.hpp`
- Modify: `src/protocol/Protocol.cpp`
- Modify: `tests/protocol/ProtocolTests.cpp`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Consumes: `mapLogicalPort`.
- Produces: `TxFrameResult encodeTxFrame(uint8_t logicalPort, const uint8_t* payload, size_t payloadLength, size_t maxPacketSize, uint8_t* output, size_t outputCapacity)`, which emits one frame and reports consumed payload bytes.

- [ ] **Step 1: Write one-frame and boundary tests before implementation**

Add:

```cpp
struct TxFrameResult {
    Error error;
    size_t outputLength;
    size_t payloadConsumed;
};

TxFrameResult encodeTxFrame(
    uint8_t logicalPort,
    const uint8_t* payload,
    size_t payloadLength,
    size_t maxPacketSize,
    uint8_t* output,
    size_t outputCapacity);
```

Test literals:

```cpp
const uint8_t payload[] = {0x00, 0x7f, 0xff};
uint8_t output[64] = {};
const auto result = ch9344::encodeTxFrame(3, payload, 3, 64, output, sizeof(output));
const uint8_t expected[] = {0x07, 0x03, 0x00, 0x00, 0x7f, 0xff};
CHECK_EQ(result.error, ch9344::Error::none);
CHECK_EQ(result.outputLength, sizeof(expected));
CHECK_EQ(result.payloadConsumed, 3U);
CHECK_BYTES(output, expected, sizeof(expected));

uint8_t longPayload[70] = {};
const auto split = ch9344::encodeTxFrame(0, longPayload, 70, 64, output, sizeof(output));
CHECK_EQ(split.outputLength, 64U);
CHECK_EQ(split.payloadConsumed, 61U);
CHECK_EQ(output[0], 0x04);
CHECK_EQ(output[1], 0x3d);
CHECK_EQ(output[2], 0x00);
```

Also assert `invalidPort`, null pointers with nonzero lengths, `maxPacketSize <= 3`, and output capacity smaller than the selected frame.
Extend `Error` with the literal cases `invalidPacketSize` and `outputTooSmall`; null inputs continue to use `invalidArgument`.

- [ ] **Step 2: Run RED**

Build the test target. Expected: undefined `encodeTxFrame`, proving the new behavior is absent. Record the exact failure.

- [ ] **Step 3: Implement one bounded frame**

The implementation maps the port, sets `payloadConsumed = min(payloadLength, maxPacketSize - 3)`, writes little-endian length bytes, copies exactly that payload, and never writes on validation failure. Do not loop or allocate; the caller owns fragmentation.

```cpp
const size_t framePayload = std::min(payloadLength, maxPacketSize - 3);
output[0] = hardwarePort;
output[1] = static_cast<uint8_t>(framePayload & 0xff);
output[2] = static_cast<uint8_t>((framePayload >> 8) & 0xff);
std::memcpy(output + 3, payload, framePayload);
return {Error::none, framePayload + 3, framePayload};
```

- [ ] **Step 4: Run GREEN, mutation-check, and commit**

Run the full protocol test. Mentally mutate port 7 to 6, length `0x3d` to `0x3c`, and the capacity comparison; the literals must catch each mutation. Run `git diff --check` and commit:

```bash
git commit -m "feat: 实现 CH9344 发送数据组帧"
```

### Task 3: RX record validation and demultiplexing

**Files:**
- Modify: `include/ch9344/Protocol.hpp`
- Modify: `src/protocol/Protocol.cpp`
- Modify: `tests/protocol/ProtocolTests.cpp`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `DecodeResult decodeRxTransfer(const uint8_t* input, size_t inputLength, RxHandler handler, void* context)`.
- Produces: `RxRecordView { logicalPort, payload, payloadLength }`.

- [ ] **Step 1: Write valid-record tests**

Define a callback that copies records into a fixed test capture. Use hand-built 32-byte and 64-byte buffers. Assert:

```cpp
struct RxRecordView {
    uint8_t logicalPort;
    const uint8_t* payload;
    size_t payloadLength;
};
using RxHandler = void (*)(void* context, const RxRecordView& record);
struct DecodeResult {
    Error error;
    size_t recordsDecoded;
    size_t bytesConsumed;
};
DecodeResult decodeRxTransfer(
    const uint8_t* input, size_t inputLength, RxHandler handler, void* context);
```

```cpp
record[0] = 0x07;
record[1] = 0x03;
record[2] = 0xa5;
record[3] = 0x00;
record[4] = 0xff;
```

Expected callback: logical port `3`, length `3`, bytes `{0xa5, 0x00, 0xff}`. A two-record fixture uses hardware ports `4` and `6` and proves they reach logical ports `0` and `2` in input order.

- [ ] **Step 2: Run RED, then implement the valid path**

Expected RED: undefined `decodeRxTransfer`. Implement a loop in 32-byte increments. Validate the handler and nonzero input pointer, map byte 0 through the inverse range `4...7`, validate byte 1 `<= 30`, then invoke the real callback with a view into bytes `2...31`.

- [ ] **Step 3: Write malformed-record tests while GREEN code exists**

Before changing production code, add separate tests for:

- 31-byte trailing record → `truncatedRxRecord`, zero callbacks;
- hardware port 3 or 8 → `invalidRxPort`, zero callbacks;
- payload length 31 → `invalidRxLength`, zero callbacks;
- null input with nonzero length → `invalidArgument`;
- empty input → success with zero records.

Extend `Error` with `truncatedRxRecord`, `invalidRxPort`, and `invalidRxLength` before running this RED cycle.

Run them and confirm at least the first missing validation fails for the expected assertion, not by crashing.

- [ ] **Step 4: Add minimum validation and run GREEN**

For a malformed record, do not call the handler for that record. `bytesConsumed` counts only complete valid records before the error.

- [ ] **Step 5: Verify and commit**

Run all protocol tests and `git diff --check`. Mutation-check the stride, valid port range, length limit, and payload offset. Commit:

```bash
git commit -m "feat: 实现 CH9344 接收数据解帧"
```

### Task 4: Chip version and initialization command encoding

**Files:**
- Modify: `include/ch9344/Protocol.hpp`
- Modify: `src/protocol/Protocol.cpp`
- Modify: `tests/protocol/ProtocolTests.cpp`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `Error parseChipVersion(const uint8_t* response, size_t length, ChipInfo* info)` for exact 4-byte responses.
- Produces: fixed-capacity `Command` and `CommandSequence`.
- Produces: `Error encodeDeviceInitialization(const ChipInfo& chip, CommandSequence* output)` and `Error encodePortInitialization(uint8_t logicalPort, CommandSequence* output)`.

- [ ] **Step 1: Write chip-version tests and observe RED**

Add types:

```cpp
enum class ChipVariant { ch9344L, ch9344Q };
struct ChipInfo { ChipVariant variant; uint8_t version; };
Error parseChipVersion(const uint8_t* response, size_t length, ChipInfo* info);
```

Fixtures `{0x3f,0,0,0}` and `{0x40,0,0,0}` must produce L and Q respectively. Length 3, length 5, and null pointers must fail. Run RED expecting the missing function, then implement only these comparisons and run GREEN.

Add `invalidVersionResponse` to `Error` for any response length other than exactly 4; null pointers use `invalidArgument`.

- [ ] **Step 2: Write upload-mode tests and observe RED**

Use:

```cpp
struct Command { uint8_t bytes[16]; size_t length; };
struct CommandSequence { Command commands[8]; size_t count; };
Error encodeDeviceInitialization(const ChipInfo& chip, CommandSequence* output);
```

CH9344Q and CH9344L version `0x39` must produce one 8-byte command `{0x94,0x9d,0x01,0,0,0,0,0}`. CH9344L version `0x38` must produce zero commands. Run RED, implement the version gate, and run GREEN.

- [ ] **Step 3: Write per-port initialization tests and observe RED**

For logical port 3, assert three literal commands:

```text
c0 3a 87
c0 3b 03
c0 3c 08
```

For logical port 0, the register base is `0x08`; invalid port 4 must fail without modifying the output. Implement `encodePortInitialization(uint8_t, CommandSequence*)` with command lengths exactly 3.

- [ ] **Step 4: Verify and commit**

Run the full protocol test and format gate. Commit:

```bash
git commit -m "feat: 实现 CH9344 芯片初始化命令"
```

### Task 5: 8N1, baud rate, DTR, and RTS command encoding

**Files:**
- Modify: `include/ch9344/Protocol.hpp`
- Modify: `src/protocol/Protocol.cpp`
- Modify: `tests/protocol/ProtocolTests.cpp`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `Error encodeUart8N1(ChipVariant variant, uint8_t logicalPort, uint32_t baudRate, CommandSequence* output)`.
- Produces: `Error encodeModemControl(uint8_t logicalPort, bool dtr, bool rts, CommandSequence* output)`.

- [ ] **Step 1: Test CH9344L 115200 8N1 and observe RED**

For logical port 3, assert this exact six-command sequence:

```text
80 39 50
20 3b 01 00 00 00
c0 3b 03
97 9c 07 02
c0 39 0f
90 85 3e
```

The second command uses the rounded `1,843,200 / 16 / baud` divisor; the fourth command uses receive timeout `5` at `>= 921600`, otherwise `(1,000,000 * 15 / baud) / 100 + 1`.

- [ ] **Step 2: Implement CH9344L minimum behavior and run GREEN**

Validate baud `1...12,000,000`. Special-case 2,000,000 to divisor bytes `02 00`, use clock 44,236,800 and the `0x51` clock selector above 115200, and emit the six commands with explicit zero-filled storage. Set baud selector byte 4 to `1,2,3,4,5,6` for `250000,500000,1000000,1500000,3000000,12000000` respectively and to `0` for other rates.

- [ ] **Step 3: Test CH9344Q direct baud and observe RED**

For CH9344Q at 115200, command two must be:

```text
20 3b 00 00 00 00 c2 01 00
```

Run RED against the L-only implementation, then add the Q branch that stores baud as little-endian bytes 5...8. Re-run GREEN.

- [ ] **Step 4: Test independent DTR/RTS values and observe RED**

For logical port 3, assert:

| DTR | RTS | Commands |
| --- | --- | --- |
| false | false | `80 3c 00`, `80 3c 10` |
| true | false | `80 3c 01`, `80 3c 10` |
| false | true | `80 3c 00`, `80 3c 11` |
| true | true | `80 3c 01`, `80 3c 11` |

Implement exactly two 3-byte commands and reject invalid ports.

- [ ] **Step 5: Verify and commit**

Run all protocol tests. Mutation-check endian order, register base, DTR and RTS independence, and 8N1 format byte `0x03`. Commit:

```bash
git commit -m "feat: 实现 CH9344 串口配置命令"
```

### Task 6: USB endpoint layout classification

**Files:**
- Create: `include/ch9344/EndpointLayout.hpp`
- Create: `src/protocol/EndpointLayout.cpp`
- Create: `tests/protocol/EndpointLayoutTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `Error classifyEndpoints(const EndpointDescriptor* endpoints, size_t count, EndpointLayout* output)`.
- Produces: `dataIn=0x82`, `dataOut=0x02`, `commandIn=0x81`, `commandOut=0x01` for the current device.

- [ ] **Step 1: Write order-independent descriptor tests and observe RED**

Define platform-neutral inputs:

```cpp
enum class EndpointType { bulk, other };
struct EndpointDescriptor { uint8_t address; EndpointType type; uint16_t maxPacketSize; };
struct EndpointLayout {
    uint8_t dataIn, dataOut, commandIn, commandOut;
    uint16_t dataMaxPacketSize, commandMaxPacketSize;
};
```

Pass the real descriptors in deliberately reversed order:

```cpp
const EndpointDescriptor endpoints[] = {
    {0x01, EndpointType::bulk, 512},
    {0x81, EndpointType::bulk, 512},
    {0x02, EndpointType::bulk, 512},
    {0x82, EndpointType::bulk, 512},
};
```

Assert the four literal addresses and both max packet sizes. Separate tests reject missing, duplicate, non-Bulk, zero-packet, and unexpected endpoint-number layouts.

- [ ] **Step 2: Implement endpoint-number classification and run GREEN**

Classify endpoint number 2 as data and endpoint number 1 as command; direction comes from bit `0x80`. Require exactly one of each and identical IN/OUT max packet sizes per pair. Never infer roles from descriptor order.

- [ ] **Step 3: Verify and commit**

Add `EndpointLayout.cpp` to the library. Build `tests/protocol/EndpointLayoutTests.cpp` as a separate `ch9344_endpoint_layout_tests` executable using `tests/TestSupport.hpp`, register CTest test `endpoint_layout`, and label it `protocol`. Run CTest and `git diff --check`, then commit:

```bash
git commit -m "feat: 识别 CH9344 USB 端点布局"
```

### Task 7: Real-device `inspect` command

**Files:**
- Create: `src/probe/LibusbDevice.hpp`
- Create: `src/probe/LibusbDevice.cpp`
- Create: `src/probe/main.cpp`
- Create: `tests/hardware/test_inspect.sh`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `ch9344-probe inspect`.
- Produces: process exits `0` success, `2` usage, `3` open/access, `4` descriptor/protocol, `5` USB transfer.

- [ ] **Step 1: Write the opt-in hardware test before probe behavior**

The script executes the real binary and asserts effects/output, not source text:

```bash
#!/bin/bash
set -euo pipefail
probe=$1
output=$("$probe" inspect)
grep -Fxq 'device 1a86:e018' <<<"$output"
grep -Fxq 'interface 0' <<<"$output"
grep -Fxq 'data-in 0x82 bulk 512' <<<"$output"
grep -Fxq 'data-out 0x02 bulk 512' <<<"$output"
grep -Fxq 'command-in 0x81 bulk 512' <<<"$output"
grep -Fxq 'command-out 0x01 bulk 512' <<<"$output"
grep -Eq '^chip CH9344[QL] version 0x[0-9a-f]{2}$' <<<"$output"
```

After writing the script, create `src/probe/main.cpp` with a temporary `main` that returns usage exit code 2 and add an otherwise empty probe target. Add `CH9344_ENABLE_HARDWARE_TESTS` defaulting OFF. When ON, register `hardware_inspect` with label `hardware` and resource lock `ch9344`. Configure with the option ON and run the test; expected RED is exit 2 and missing inspect output, not a compilation or script syntax error.

Register the test with the built executable path rather than assuming the build directory:

```cmake
add_test(NAME hardware_inspect
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/hardware/test_inspect.sh
            $<TARGET_FILE:ch9344_probe>)
set_tests_properties(hardware_inspect PROPERTIES
    LABELS hardware RESOURCE_LOCK ch9344)
```

- [ ] **Step 2: Add the probe target and libusb RAII boundary**

Use:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
add_executable(ch9344_probe src/probe/main.cpp src/probe/LibusbDevice.cpp)
set_target_properties(ch9344_probe PROPERTIES OUTPUT_NAME ch9344-probe)
target_link_libraries(ch9344_probe PRIVATE ch9344_protocol PkgConfig::LIBUSB)
```

`LibusbDevice` owns `libusb_context*`, `libusb_device_handle*`, the claimed-interface flag, and the active configuration descriptor only while parsing it. Destruction releases interface 0 if claimed, closes the handle, then exits the context.

- [ ] **Step 3: Implement exact discovery and version read**

The `inspect` path performs, in order:

```cpp
libusb_init(&context);
handle = libusb_open_device_with_vid_pid(context, 0x1a86, 0xe018);
libusb_get_device_descriptor(libusb_get_device(handle), &deviceDescriptor);
libusb_get_active_config_descriptor(libusb_get_device(handle), &config);
```

Require one altsetting for interface 0 and convert all four libusb descriptors into `EndpointDescriptor`. Feed them to `classifyEndpoints`, then free the config descriptor. Read version with:

```cpp
const int transferred = libusb_control_transfer(
    handle, 0xc0, 0x96, 0, 0, versionBytes, 4, 5000);
```

Require exactly 4 bytes and call `parseChipVersion`. Print the seven stable lines asserted by the script. Include libusb error names on stderr without changing the stable stdout contract.

- [ ] **Step 4: Run GREEN and commit**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCH9344_ENABLE_HARDWARE_TESTS=ON
cmake --build build
ctest --test-dir build -L hardware --output-on-failure
ctest --test-dir build -L protocol --output-on-failure
git diff --check
```

Expected: real descriptor and version test passes. Document the build/run command in README and commit:

```bash
git commit -m "feat: 实现 CH9344 USB 设备检查"
```

### Task 8: Fourth-port loopback command

**Files:**
- Modify: `src/probe/LibusbDevice.hpp`
- Modify: `src/probe/LibusbDevice.cpp`
- Modify: `src/probe/main.cpp`
- Create: `tests/hardware/test_loopback_port4.sh`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/verification/tdd/2026-08-31-protocol-validation.md`

**Interfaces:**
- Produces: `ch9344-probe loopback --port 4 --baud 115200 --payload-hex <hex>`.
- Produces: `ch9344-probe loopback --port 4 --baud <rate> --length <count>` with deterministic bytes.

- [ ] **Step 1: Write the real loopback test and observe RED**

```bash
#!/bin/bash
set -euo pipefail
probe=$1
payload='00017f80ff4348393334342d544444a5'
output=$("$probe" loopback --port 4 --baud 115200 --payload-hex "$payload")
grep -Fxq 'PASS port=4 baud=115200 bytes=16' <<<"$output"
```

Register it as `hardware_loopback_port4`, label `hardware`, and use the same `ch9344` resource lock. Run only this test. Expected RED: exit 2 for an unknown `loopback` command; confirm it is not a USB permission failure.

- [ ] **Step 2: Implement claim, command OUT, and cleanup**

Claim interface 0 with `libusb_claim_interface`. Treat `LIBUSB_ERROR_NOT_SUPPORTED` from auto-detach setup as nonfatal on macOS; all other errors are explicit. Send every protocol command to endpoint `0x01` with a 5-second timeout and require `transferred == command.length`.

Initialization order is fixed:

1. version control IN and `parseChipVersion`;
2. `encodeDeviceInitialization`;
3. `encodePortInitialization(logicalPort)`;
4. `encodeUart8N1(variant, logicalPort, baud)`;
5. `encodeModemControl(logicalPort, true, true)`.

After each command batch, drain available command/status input from endpoint `0x81` with bounded 20 ms reads until timeout. A timeout means the drain is complete; other libusb errors remain fatal. The first phase records status bytes but does not interpret unsupported GPIO or flow-control events.

On every post-claim exit path, best-effort send DTR/RTS false, release interface 0, and close the handle.

- [ ] **Step 3: Implement framed TX and demultiplexed RX**

Convert user port `4` to logical port `3`. Parse hex only when it has an even number of valid hex characters. For `--length`, generate byte `i` as `(i * 37 + 0xa5) & 0xff` so expected data is deterministic without using protocol helpers.

Drain stale Data IN traffic with 20 ms reads until timeout. Repeatedly call `encodeTxFrame` using the discovered 512-byte max packet size and send each frame to endpoint `0x02`, requiring a full write.

Until the 5-second deadline, read up to 512 bytes from endpoint `0x82`, call `decodeRxTransfer`, ignore valid records belonging to logical ports `0...2`, and append only logical port 3 payload. Fail with exit 6 on malformed RX, timeout, extra bytes, or byte mismatch. Success prints exactly:

```text
PASS port=4 baud=115200 bytes=16
```

- [ ] **Step 4: Run GREEN, all tests, and commit**

Run the specific hardware test first, then protocol and all enabled hardware tests. Run `git diff --check`. Commit:

```bash
git commit -m "feat: 实现 CH9344 第四路回环命令"
```

### Task 9: Hardware acceptance evidence

**Files:**
- Create: `docs/verification/2026-08-31-port4-loopback.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: completed probe and current fourth-port TX/RX short.
- Produces: reproducible evidence without claiming untestable first-to-third-port RX or DTR/RTS voltage.

- [ ] **Step 1: Run the fresh acceptance matrix**

Run all unit and hardware tests, then these additional real-device cases:

```bash
build/ch9344-probe loopback --port 4 --baud 9600 --length 1
build/ch9344-probe loopback --port 4 --baud 115200 --length 16
build/ch9344-probe loopback --port 4 --baud 115200 --length 509
build/ch9344-probe loopback --port 4 --baud 921600 --length 509
```

Every command must exit 0 and print the matching PASS line. If any case fails, do not write a passing report; return to a new failing regression test for that defect.

- [ ] **Step 2: Record exact evidence and limitations**

The verification document records date, commit under test, USB ID, endpoint layout, chip variant/version, commands, exit codes, byte counts, and current wiring. It explicitly states:

- only physical port 4 had TX/RX shorted;
- ports 1...3 did not receive physical RX validation;
- DTR/RTS command transfer was exercised, but pin voltage was not measured;
- this phase does not publish `/dev/cu.*` or `/dev/tty.*`.

- [ ] **Step 3: Final verification and commit**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
git status --short
```

Review every Global Constraint against the diff. Stage only README and the hardware evidence document, then commit:

```bash
git commit -m "test: 验证 CH9344 第四路硬件回环"
```

After this commit, write a separate DriverKit single-port spike plan using the measured chip variant, endpoint behavior, and protocol fixtures from this plan.
