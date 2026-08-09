/// A counting allocator, so the benchmark can report memory it actually
/// measured rather than memory it guessed.
///
/// C++ has no equivalent of Python's `tracemalloc`, and peak RSS is far too
/// coarse: it never falls, so a second algorithm measured after a first sees
/// nothing. Replacing the global `operator new`/`operator delete` with a pair
/// that keeps a running total is the closest honest analogue -- it counts
/// bytes the program asked for, ignoring allocator overhead. Memory mapped
/// behind the standard library's back stays unseen unless whoever mapped it
/// reports it through `note_mapped`.
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

/// Account for a mapping made behind `operator new`'s back -- the BFS mark
/// array's huge-page block -- so the benchmark still sees it.
void note_mapped(std::size_t bytes);

/// The mapping is gone; stop counting it against the live total.
void forget_mapped(std::size_t bytes);

}  // namespace rubik::memtrack
