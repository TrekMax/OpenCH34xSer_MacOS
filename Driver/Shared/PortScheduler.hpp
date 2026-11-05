#pragma once

#include <cstddef>
#include <cstdint>

#include <ch9344/Protocol.hpp>

namespace ch9344::driver {

constexpr std::uint8_t kPortCount = 4;
constexpr std::uint8_t kAllPortsMask = 0x0f;

struct TxPortSelection {
    bool found;
    std::uint8_t logicalPort;
    std::uint8_t nextCursor;
};

TxPortSelection selectNextTxPort(
    std::uint8_t readyMask,
    std::uint8_t cursor);

struct TxFlowControl {
    std::uint8_t readyMask;

    void reset();
    TxPortSelection select(std::uint8_t pendingMask, std::uint8_t cursor) const;
    void submitted(std::uint8_t logicalPort);
    void ready(std::uint8_t portMask);
};

struct CommandStatusInspection {
    bool valid;
    std::uint8_t txReadyMask;
};

CommandStatusInspection inspectCommandStatus(
    const std::uint8_t* transfer,
    std::size_t transferLength);

struct RxPortInspection {
    ch9344::Error error;
    std::uint8_t portMask;
};

RxPortInspection inspectRxPorts(
    const std::uint8_t* transfer,
    std::size_t transferLength);

std::uint8_t markRxPortDelivered(
    std::uint8_t pendingMask,
    std::uint8_t logicalPort);

} // namespace ch9344::driver
