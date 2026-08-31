#include "../TestSupport.hpp"

#include <ch9344/EndpointLayout.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace {

constexpr std::array<ch9344::EndpointDescriptor, 4> kValidEndpoints {{
    {0x01, ch9344::EndpointType::bulk, 512},
    {0x81, ch9344::EndpointType::bulk, 512},
    {0x02, ch9344::EndpointType::bulk, 512},
    {0x82, ch9344::EndpointType::bulk, 512},
}};

void testClassifiesRealEndpointsWithoutDependingOnOrder()
{
    ch9344::EndpointLayout output {};

    CHECK_EQ(
        ch9344::classifyEndpoints(
            kValidEndpoints.data(),
            kValidEndpoints.size(),
            &output),
        ch9344::Error::none);
    CHECK_EQ(output.dataIn, 0x82);
    CHECK_EQ(output.dataOut, 0x02);
    CHECK_EQ(output.commandIn, 0x81);
    CHECK_EQ(output.commandOut, 0x01);
    CHECK_EQ(output.dataMaxPacketSize, 512);
    CHECK_EQ(output.commandMaxPacketSize, 512);
}

void checkRejected(const ch9344::EndpointDescriptor* endpoints, std::size_t count)
{
    ch9344::EndpointLayout output {
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5a5, 0xa5a5,
    };

    CHECK_EQ(
        ch9344::classifyEndpoints(endpoints, count, &output),
        ch9344::Error::invalidEndpointLayout);
    CHECK_EQ(output.dataIn, 0xa5);
    CHECK_EQ(output.dataMaxPacketSize, 0xa5a5);
}

void testRejectsMissingAndDuplicateEndpoints()
{
    checkRejected(kValidEndpoints.data(), 3);

    const ch9344::EndpointDescriptor duplicate[] = {
        {0x01, ch9344::EndpointType::bulk, 512},
        {0x81, ch9344::EndpointType::bulk, 512},
        {0x81, ch9344::EndpointType::bulk, 512},
        {0x82, ch9344::EndpointType::bulk, 512},
    };
    checkRejected(duplicate, std::size(duplicate));
}

void testRejectsWrongTypePacketSizeAndEndpointNumber()
{
    auto wrongType = kValidEndpoints;
    wrongType[0].type = ch9344::EndpointType::other;
    checkRejected(wrongType.data(), wrongType.size());

    auto zeroPacket = kValidEndpoints;
    zeroPacket[0].maxPacketSize = 0;
    checkRejected(zeroPacket.data(), zeroPacket.size());

    auto unexpectedNumber = kValidEndpoints;
    unexpectedNumber[0].address = 0x03;
    checkRejected(unexpectedNumber.data(), unexpectedNumber.size());

    auto mismatchedPacket = kValidEndpoints;
    mismatchedPacket[3].maxPacketSize = 64;
    checkRejected(mismatchedPacket.data(), mismatchedPacket.size());
}

void testValidatesPointers()
{
    ch9344::EndpointLayout output {};
    CHECK_EQ(
        ch9344::classifyEndpoints(nullptr, 4, &output),
        ch9344::Error::invalidArgument);
    CHECK_EQ(
        ch9344::classifyEndpoints(
            kValidEndpoints.data(),
            kValidEndpoints.size(),
            nullptr),
        ch9344::Error::invalidArgument);
}

} // namespace

int main()
{
    testClassifiesRealEndpointsWithoutDependingOnOrder();
    testRejectsMissingAndDuplicateEndpoints();
    testRejectsWrongTypePacketSizeAndEndpointNumber();
    testValidatesPointers();
    return test_support::failures == 0 ? 0 : 1;
}
