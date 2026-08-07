#include "rubik/display.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

namespace rubik::display {
namespace {

struct Rgb {
    int red;
    int green;
    int blue;
};

/// Face colours close to a real cube. This puzzle uses purple where a standard
/// cube has orange.
constexpr std::array<Rgb, 6> palette{{
    {196, 30, 58},    // red
    {245, 245, 245},  // white
    {0, 81, 186},     // blue
    {255, 213, 0},    // yellow
    {137, 66, 175},   // purple
    {0, 158, 96},     // green
}};

constexpr std::size_t cell_width = 4;
constexpr std::size_t face_width = cell_width * 2;
constexpr std::string_view gap = "  ";
constexpr std::string_view reset = "\x1b[0m";
constexpr std::string_view dim = "\x1b[2m";

constexpr std::array<std::pair<std::string_view, Side>, 4> band{{
    {"LEFT", Side::left},
    {"FRONT", Side::front},
    {"RIGHT", Side::right},
    {"BACK", Side::back},
}};

/// Rows per facelet: two for colour blocks, one for letters. Terminal cells are
/// about twice as tall as they are wide, so a colour block needs two rows to
/// come out square; the letter fallback uses one, where squareness would only
/// cost readability.
constexpr std::size_t cell_height(ColorMode color) { return color == ColorMode::none ? 1 : 2; }

[[nodiscard]] std::string centered(std::string_view text, std::size_t width) {
    if (text.size() >= width) {
        return std::string{text};
    }
    const std::size_t left = (width - text.size()) / 2;
    return std::string(left, ' ') + std::string{text} + std::string(width - text.size() - left, ' ');
}

/// Nearest index in the xterm 6x6x6 colour cube.
[[nodiscard]] int ansi256(Rgb rgb) {
    return 16 + 36 * (rgb.red * 5 / 255) + 6 * (rgb.green * 5 / 255) + (rgb.blue * 5 / 255);
}

[[nodiscard]] std::string paint(Color facelet, ColorMode color) {
    const auto id = std::to_underlying(facelet);
    if (color == ColorMode::none) {
        const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(color_chars[id])));
        return centered(std::string(1, letter), cell_width);
    }
    const Rgb rgb = palette[id];
    const std::string background =
        color == ColorMode::truecolor
            ? std::format("\x1b[48;2;{};{};{}m", rgb.red, rgb.green, rgb.blue)
            : std::format("\x1b[48;5;{}m", ansi256(rgb));
    return background + std::string(cell_width, ' ') + std::string{reset};
}

/// One face as text rows.
[[nodiscard]] std::vector<std::string> face(const State& state, Side side, ColorMode color) {
    const std::size_t base = std::to_underlying(side) * 4;
    const std::array<std::array<std::size_t, 2>, 2> order =
        side == Side::back ? std::array<std::array<std::size_t, 2>, 2>{{{3, 2}, {1, 0}}}
                           : std::array<std::array<std::size_t, 2>, 2>{{{0, 1}, {2, 3}}};

    std::vector<std::string> rows;
    for (const auto& pair : order) {
        std::string painted;
        for (std::size_t cell : pair) {
            painted += paint(state[base + cell], color);
        }
        for (std::size_t repeat = 0; repeat < cell_height(color); ++repeat) {
            rows.push_back(painted);
        }
    }
    return rows;
}

[[nodiscard]] std::string label(std::string_view text, ColorMode color) {
    const std::string body = centered(text, face_width);
    return color == ColorMode::none ? body : std::string{dim} + body + std::string{reset};
}

}  // namespace

ColorMode best_color() {
    const char* term = std::getenv("TERM");
    if (term != nullptr && std::string_view{term} == "dumb") {
        return ColorMode::none;
    }
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm != nullptr) {
        const std::string_view value{colorterm};
        if (value == "truecolor" || value == "24bit") {
            return ColorMode::truecolor;
        }
    }
    return ColorMode::ansi256;
}

ColorMode detect_color() {
    if (std::getenv("NO_COLOR") != nullptr) {
        return ColorMode::none;
    }
    if (isatty(fileno(stdout)) == 0 && std::getenv("FORCE_COLOR") == nullptr) {
        return ColorMode::none;
    }
    return best_color();
}

std::string render(const State& state, ColorMode color) {
    // UP and DOWN sit over FRONT, the second face in the band.
    const std::string indent(face_width + gap.size(), ' ');

    std::vector<std::string> lines;
    lines.push_back(indent + label("UP", color));
    for (const std::string& row : face(state, Side::up, color)) {
        lines.push_back(indent + row);
    }
    lines.emplace_back();

    std::string headings;
    for (const auto& [name, side] : band) {
        if (!headings.empty()) {
            headings += gap;
        }
        headings += label(name, color);
    }
    lines.push_back(headings);

    std::vector<std::vector<std::string>> faces;
    for (const auto& [name, side] : band) {
        faces.push_back(face(state, side, color));
    }
    for (std::size_t row = 0; row < faces.front().size(); ++row) {
        std::string line;
        for (const std::vector<std::string>& drawn : faces) {
            if (!line.empty()) {
                line += gap;
            }
            line += drawn[row];
        }
        lines.push_back(line);
    }

    lines.emplace_back();
    lines.push_back(indent + label("DOWN", color));
    for (const std::string& row : face(state, Side::down, color)) {
        lines.push_back(indent + row);
    }

    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            text += '\n';
        }
        text += lines[i];
    }
    return text;
}

}  // namespace rubik::display
