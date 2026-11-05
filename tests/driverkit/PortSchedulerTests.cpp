#include "../../Driver/Shared/PortScheduler.hpp"
#include "../TestSupport.hpp"

#include <array>

namespace {

void testSchedulesReadyPortsRoundRobin()
{
    auto selected = ch9344::driver::selectNextTxPort(0b0101, 0);
    CHECK_EQ(selected.found, true);
    CHECK_EQ(selected.logicalPort, 0U);
    CHECK_EQ(selected.nextCursor, 1U);

    selected = ch9344::driver::selectNextTxPort(0b0101, selected.nextCursor);
    CHECK_EQ(selected.found, true);
    CHECK_EQ(selected.logicalPort, 2U);
    CHECK_EQ(selected.nextCursor, 3U);

    selected = ch9344::driver::selectNextTxPort(0b0101, selected.nextCursor);
    CHECK_EQ(selected.found, true);
    CHECK_EQ(selected.logicalPort, 0U);
}

void testIgnoresInvalidReadyBitsAndNormalizesCursor()
{
    const auto none = ch9344::driver::selectNextTxPort(0xf0, 9);
    CHECK_EQ(none.found, false);
    CHECK_EQ(none.nextCursor, 1U);

    const auto port4 = ch9344::driver::selectNextTxPort(0b1000, 7);
    CHECK_EQ(port4.found, true);
    CHECK_EQ(port4.logicalPort, 3U);
    CHECK_EQ(port4.nextCursor, 0U);
}

void testInspectsInterleavedRxPortsOnce()
{
    std::array<std::uint8_t, 96> transfer {};
    transfer[0] = 0x04;
    transfer[1] = 1;
    transfer[2] = 0xa0;
    transfer[32] = 0x07;
    transfer[33] = 2;
    transfer[34] = 0xd0;
    transfer[35] = 0xd1;
    transfer[64] = 0x05;
    transfer[65] = 1;
    transfer[66] = 0xb0;

    const auto inspection = ch9344::driver::inspectRxPorts(
        transfer.data(), transfer.size());
    CHECK_EQ(inspection.error, ch9344::Error::none);
    CHECK_EQ(inspection.portMask, 0b1011U);

    std::uint8_t pending = inspection.portMask;
    pending = ch9344::driver::markRxPortDelivered(pending, 3);
    CHECK_EQ(pending, 0b0011U);
    pending = ch9344::driver::markRxPortDelivered(pending, 0);
    CHECK_EQ(pending, 0b0010U);
    CHECK_EQ(ch9344::driver::markRxPortDelivered(pending, 8), pending);
}

void testRejectsMalformedRxBeforeDispatch()
{
    std::array<std::uint8_t, 32> transfer {};
    transfer[0] = 0x07;
    transfer[1] = 31;

    const auto inspection = ch9344::driver::inspectRxPorts(
        transfer.data(), transfer.size());
    CHECK_EQ(inspection.error, ch9344::Error::invalidRxLength);
    CHECK_EQ(inspection.portMask, 0U);
}

void testCommandStatusSeparatesRepliesFromTxEmpty()
{
    const std::uint8_t status[] = {
        0x94, 0xa1, 0x07, 0x02, 0, 0, 0, 0, 0, 0, 0, 0,
        0x97, 0x9c, 0x07, 0x02,
        0x97, 0x85, 0x3e, 0x02,
        0x94, 0x00, 0x02,
        0x95, 0x06, 0x02,
        0x94, 0x02, 0,
        0x97, 0x12, 0,
    };
    const auto decoded = ch9344::driver::inspectCommandStatus(status, sizeof(status));
    CHECK_EQ(decoded.valid, true);
    CHECK_EQ(decoded.txReadyMask, 0b1001U);
    for (const auto length : {1U, 2U, 11U, 15U, 19U, 31U}) {
        const auto truncated = ch9344::driver::inspectCommandStatus(status, length);
        CHECK_EQ(truncated.valid, false);
        CHECK_EQ(truncated.txReadyMask, 0U);
    }
    const std::uint8_t invalidPort[] = {0x93, 0x02, 0};
    CHECK_EQ(ch9344::driver::inspectCommandStatus(invalidPort, 3).valid, false);
    const std::uint8_t unknown[] = {0x97, 0xff, 0, 0x97, 0x02, 0};
    CHECK_EQ(ch9344::driver::inspectCommandStatus(unknown, 6).txReadyMask, 0U);
    CHECK_EQ(ch9344::driver::inspectCommandStatus(nullptr, 0).valid, true);
    CHECK_EQ(ch9344::driver::inspectCommandStatus(nullptr, 3).valid, false);
}

void testTxWaitsForItsOwnUartEmptyNotification()
{
    ch9344::driver::TxFlowControl flow {};
    flow.reset();
    CHECK_EQ(flow.select(0b1000, 0).found, true);
    flow.submitted(3);
    // USB completion frees the host buffer; it grants no UART credit.
    CHECK_EQ(flow.select(0b1000, 0).found, false);
    CHECK_EQ(flow.select(0b1001, 0).logicalPort, 0U);
    flow.ready(0b0001);
    CHECK_EQ(flow.select(0b1000, 0).found, false);
    flow.ready(0b1000);
    CHECK_EQ(flow.select(0b1000, 0).logicalPort, 3U);
    // Duplicate notifications must not accumulate additional credit.
    flow.ready(0b1000);
    flow.submitted(3);
    CHECK_EQ(flow.select(0b1000, 0).found, false);
}

} // namespace

int main()
{
    testSchedulesReadyPortsRoundRobin();
    testIgnoresInvalidReadyBitsAndNormalizesCursor();
    testInspectsInterleavedRxPortsOnce();
    testRejectsMalformedRxBeforeDispatch();
    testCommandStatusSeparatesRepliesFromTxEmpty();
    testTxWaitsForItsOwnUartEmptyNotification();
    return test_support::failures == 0 ? 0 : 1;
}
