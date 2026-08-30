#include "../TestSupport.hpp"

#include <ch9344/Protocol.hpp>

#include <cstdint>

namespace {

void testLogicalPortMapping()
{
    const uint8_t expected[] = {4, 5, 6, 7};

    for (uint8_t logical = 0; logical < 4; ++logical) {
        uint8_t hardware = 0;
        CHECK_EQ(
            ch9344::mapLogicalPort(logical, &hardware),
            ch9344::Error::none);
        CHECK_EQ(hardware, expected[logical]);
    }

    uint8_t untouched = 0xa5;
    CHECK_EQ(
        ch9344::mapLogicalPort(4, &untouched),
        ch9344::Error::invalidPort);
    CHECK_EQ(untouched, 0xa5);
    CHECK_EQ(
        ch9344::mapLogicalPort(0, nullptr),
        ch9344::Error::invalidArgument);
}

} // namespace

int main()
{
    testLogicalPortMapping();
    return test_support::failures == 0 ? 0 : 1;
}
