#pragma once

#include <cstddef>

namespace apollonian::core {

// Hardware cache line size used across the library to align hot fields and
// avoid false sharing between threads.
//
// std::hardware_destructive_interference_size is deliberately NOT used here:
// GCC and Clang both warn that its value depends on -mtune/-march and can
// differ between translation units compiled with different flags, which is
// exactly the kind of ABI hazard a library header must not introduce (two
// TUs disagreeing on this value would disagree on struct layout too - see
// https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
// and GCC's own -Winterference-size diagnostic). A fixed, self-defined
// constant is both simpler and more correct. 64 bytes covers the vast
// majority of modern x86-64 and ARM64 cores.
inline constexpr std::size_t kCacheLineSize = 64;

}  // namespace apollonian::core
