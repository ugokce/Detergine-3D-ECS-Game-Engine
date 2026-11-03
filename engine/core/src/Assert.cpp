#include "dg/core/Assert.h"

#include "dg/core/Log.h"

#include <cstdlib>

namespace dg::detail {

void assertFailed(const char* expression, const char* message, std::source_location location) {
    log::write(log::Level::Fatal,
               std::format("Assertion failed: {}{}{} ({}:{} in {})", expression, message ? " — " : "",
                           message ? message : "", location.file_name(), location.line(), location.function_name()));
    std::abort();
}

} // namespace dg::detail
