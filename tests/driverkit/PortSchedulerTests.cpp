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

} // namespace

int main()
{
    testSchedulesReadyPortsRoundRobin();
    testIgnoresInvalidReadyBitsAndNormalizesCursor();
    testInspectsInterleavedRxPortsOnce();
    testRejectsMalformedRxBeforeDispatch();
    return test_support::failures == 0 ? 0 : 1;
}
