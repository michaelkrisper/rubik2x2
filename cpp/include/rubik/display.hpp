/// Drawing a cube for a terminal.
///
/// Deliberately separate from `rubik::cube`: the model is 24 bytes and knows
/// nothing about how anyone looks at it.
///
/// The picture is the standard unfolded net -- UP above FRONT, the four side
/// faces in a band, DOWN below -- so it matches the order a state string is
/// written in. BACK is drawn as seen from behind, which is what makes the band
/// read as one continuous strip around the cube.
#pragma once

#include <string>

#include "rubik/cube.hpp"

namespace rubik::display {

/// How much colour the output may use.
enum class ColorMode { none, ansi256, truecolor };

/// The richest mode this terminal understands, ignoring where output goes.
[[nodiscard]] ColorMode best_color();

/// What to actually use, honouring NO_COLOR, FORCE_COLOR and piping.
[[nodiscard]] ColorMode detect_color();

/// Draw the cube as an unfolded net.
[[nodiscard]] std::string render(const State& state, ColorMode color);

}  // namespace rubik::display
