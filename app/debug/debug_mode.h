#pragma once

#include <cstdint>

namespace debug_mode {

[[noreturn]] void run(std::uint32_t hw_failed);

}
