/// A hint that a cache line will be wanted shortly.
///
/// Both search loops that matter -- the distance table build and bidirectional
/// search -- walk a frontier and touch one *random* byte or word of a multi
/// megabyte array per edge. That array is larger than the last level cache of
/// an ordinary machine, so nearly every touch misses and the loop is bound by
/// memory latency rather than by anything it computes. Naming the addresses a
/// few frontier entries early lets those misses overlap with work instead of
/// stalling on each other.
///
/// C++ has no standard spelling for this. Both compilers this project builds
/// under agree on `__builtin_prefetch`; anywhere else the loops are simply
/// unhinted and still correct, since a prefetch has no semantics beyond speed.
#pragma once

namespace rubik {

inline void prefetch([[maybe_unused]] const void* address) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(address);
#endif
}

/// How many frontier entries ahead to look. Tuned by measurement: on the
/// reference machine 4 and 8 are within noise of each other, 16 and 32 are
/// clearly worse -- the line is evicted again before the loop arrives.
inline constexpr unsigned prefetch_distance = 8;

}  // namespace rubik
