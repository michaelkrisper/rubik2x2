/// A counting allocator, so the benchmark can report memory it actually
/// measured rather than memory it guessed.
///
/// C++ has no equivalent of Python's `tracemalloc`, and peak RSS is far too
/// coarse: it never falls, so a second algorithm measured after a first sees
/// nothing. Replacing the global `operator new`/`operator delete` with a pair
/// that keeps a running total is the closest honest analogue -- it counts
/// bytes the program asked for, ignoring allocator overhead and anything the
/// process maps behind the standard library's back.
#pragma once

#include <cstddef>

namespace rubik::memtrack {

/// Start a fresh measurement from whatever is live right now.
void reset();

/// The high-water mark of live bytes since the last `reset()`, above the
/// baseline that was live at that moment.
[[nodiscard]] std::size_t peak_bytes();

/// Bytes live right now, above the baseline.
[[nodiscard]] std::size_t live_bytes();

}  // namespace rubik::memtrack
