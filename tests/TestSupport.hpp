#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace test_support {

inline int failures = 0;

inline void reportFailure(
    const char* file,
    int line,
    const char* actualExpression,
    const char* expectedExpression)
{
    std::cerr << file << ':' << line << ": expected " << actualExpression
              << " == " << expectedExpression << '\n';
    ++failures;
}

inline void checkBytes(
    const uint8_t* actual,
    const uint8_t* expected,
    std::size_t length,
    const char* file,
    int line)
{
    for (std::size_t index = 0; index < length; ++index) {
        if (actual[index] != expected[index]) {
            std::cerr << file << ':' << line << ": byte " << index
                      << " differs: actual=" << static_cast<unsigned>(actual[index])
                      << ", expected=" << static_cast<unsigned>(expected[index]) << '\n';
            ++failures;
            return;
        }
    }
}

} // namespace test_support

#define CHECK_EQ(actualExpression, expectedExpression)                              \
    do {                                                                            \
        const auto actualValue = (actualExpression);                                \
        const auto expectedValue = (expectedExpression);                            \
        if (!(actualValue == expectedValue)) {                                      \
            test_support::reportFailure(                                            \
                __FILE__, __LINE__, #actualExpression, #expectedExpression);        \
        }                                                                           \
    } while (false)

#define CHECK_BYTES(actual, expected, length)                                       \
    test_support::checkBytes((actual), (expected), (length), __FILE__, __LINE__)
