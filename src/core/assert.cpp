#include "astra2d/core/assert.hpp"

#include "astra2d/core/log.hpp"

#include <cstdlib>

namespace astra2d::detail {

void assertFailed(const char* expression, const char* file, int line, std::string_view message)
{
    if (message.empty()) {
        ASTRA_CORE_CRITICAL("Assertion failed: {} ({}:{})", expression, file, line);
    } else {
        ASTRA_CORE_CRITICAL("Assertion failed: {} ({}:{}): {}", expression, file, line, message);
    }
    log::engine().flush();
    std::abort();
}

} // namespace astra2d::detail
