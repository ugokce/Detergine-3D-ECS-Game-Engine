#pragma once

#include <source_location>

namespace dg::detail {
[[noreturn]] void assertFailed(const char* expression, const char* message, std::source_location location);
} // namespace dg::detail

// DG_ASSERT: programmer-error check, compiled out in Release (DG_ASSERTS_ENABLED=0).
// DG_VERIFY: the expression is always evaluated; only the check is compiled out.
#if DG_ASSERTS_ENABLED
    #define DG_ASSERT(cond) \
        ((cond) ? void(0) : ::dg::detail::assertFailed(#cond, nullptr, std::source_location::current()))
    #define DG_ASSERT_MSG(cond, msg) \
        ((cond) ? void(0) : ::dg::detail::assertFailed(#cond, (msg), std::source_location::current()))
    #define DG_VERIFY(cond) DG_ASSERT(cond)
#else
    #define DG_ASSERT(cond) ((void)0)
    #define DG_ASSERT_MSG(cond, msg) ((void)0)
    #define DG_VERIFY(cond) ((void)(cond))
#endif
