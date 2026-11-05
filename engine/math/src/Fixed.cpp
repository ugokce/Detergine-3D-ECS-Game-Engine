#include "dg/math/Fixed.h"

#include <charconv>
#include <system_error>

namespace dg::detail {

std::string formatFixed(u64 magnitude, bool negative, int fracBits, int precision) {
    // fraction * 10 must fit in 64 bits.
    DG_ASSERT(fracBits > 0 && fracBits <= 60);
    const u64 fracMask = (u64{1} << fracBits) - 1;
    u64 integerPart = magnitude >> fracBits;
    u64 fraction = magnitude & fracMask;

    // Each step multiplies the remaining binary fraction by 10 and takes the integer carry as the next decimal digit.
    // A binary fraction with F bits has at most F decimal digits, so the exact form always terminates.
    std::string digits;
    if (precision < 0) {
        while (fraction != 0) {
            fraction *= 10;
            digits.push_back(static_cast<char>('0' + (fraction >> fracBits)));
            fraction &= fracMask;
        }
    } else {
        digits.reserve(static_cast<usize>(precision));
        for (int i = 0; i < precision; ++i) {
            fraction *= 10;
            digits.push_back(static_cast<char>('0' + (fraction >> fracBits)));
            fraction &= fracMask;
        }
        // Ties away from zero: the remainder is compared against exactly one half.
        if (fraction >= (u64{1} << (fracBits - 1))) {
            bool carry = true;
            for (usize i = digits.size(); carry && i > 0; --i) {
                if (digits[i - 1] == '9') {
                    digits[i - 1] = '0';
                } else {
                    ++digits[i - 1];
                    carry = false;
                }
            }
            integerPart += carry ? 1u : 0u;
        }
    }

    char integerText[24] = {};
    const std::to_chars_result converted = std::to_chars(integerText, integerText + sizeof(integerText), integerPart);
    DG_ASSERT(converted.ec == std::errc{});
    char* end = converted.ptr;

    std::string text;
    text.reserve(static_cast<usize>(end - integerText) + digits.size() + 2);
    if (negative) {
        text.push_back('-');
    }
    text.append(integerText, end);
    if (!digits.empty()) {
        text.push_back('.');
        text.append(digits);
    }
    return text;
}

void fixedCompileError(const char* reason) {
    // Only reachable from a runtime-parsed format string (std::vformat) with an unsupported spec. Exceptions are
    // unavailable in Shipping builds, so this asserts in development builds and the bad spec is ignored otherwise.
    DG_ASSERT_MSG(false, reason);
    (void)reason;
}

} // namespace dg::detail
