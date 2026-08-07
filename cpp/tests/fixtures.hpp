/// Data shared by several test files, built once because the distance table
/// costs a couple of hundred milliseconds.
#pragma once

#include <span>

#include "rubik/cube.hpp"
#include "rubik/table.hpp"

namespace fixtures {

const rubik::DistanceTable& table();

/// A fixed set of scrambles, so a failure is always reproducible.
std::span<const rubik::State> scrambles();

/// The antipodal position: 14 quarter turns from solved.
const rubik::State& hardest();

}  // namespace fixtures
