/// Command line entry point.
///
/// Subcommands are deliberately thin: they parse, call into the library and
/// print. `solve` writes nothing but the move sequence, so it pipes.
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <iterator>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rubik/bench.hpp"
#include "rubik/bfs.hpp"
#include "rubik/coords.hpp"
#include "rubik/cube.hpp"
#include "rubik/display.hpp"
#include "rubik/ida.hpp"
#include "rubik/table.hpp"

namespace {

using namespace rubik;

constexpr std::string_view usage_text = R"(rubik2x2 -- optimal 2x2x2 Rubik's cube solver

  rubik2x2 solve <state> [--algorithm bfs|ida|table]   print a shortest solution
  rubik2x2 scramble [-n N] [--seed S]                  print a scrambled state
  rubik2x2 show <state> [--color|--no-color]           draw a state
  rubik2x2 table [--workers N]                         build the table, print its histogram
  rubik2x2 bench [--json] [--scrambles FILE] [-n N]    compare the algorithms

A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK LEFT
RIGHT, each face read row-major; '-' reads it from stdin. Moves are quarter
turns, lowercase and uppercase being inverses: l/L r/R t/T b/B f/F a/A.
)";

/// The distance table is the reference every algorithm is checked against, so
/// its histogram is the reference every implementation is checked against.
constexpr std::array<std::uint64_t, 15> oeis_a079762{1,     6,      27,      120,     534,
                                                     2256,  8969,   33058,   114149,  360508,
                                                     930588, 1350852, 782536, 90280,   276};

[[noreturn]] void die(std::string_view message, int code) {
    std::cerr << std::format("rubik2x2: {}\n", message);
    std::exit(code);
}

int report(const Error& error) {
    std::cerr << std::format("rubik2x2: {}\n", error.message);
    return error.kind == ErrorKind::unsolvable ? 1 : 2;
}

[[nodiscard]] std::optional<long> to_number(std::string_view text) {
    long value = 0;
    const auto [pointer, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    return code == std::errc{} && pointer == text.data() + text.size() ? std::optional{value}
                                                                      : std::nullopt;
}

[[nodiscard]] std::string read_stdin() {
    return std::string{std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Result<State> state_argument(std::string_view text) {
    return text == "-" ? parse(read_stdin()) : parse(text);
}

int command_solve(std::span<const std::string_view> args) {
    std::optional<std::string_view> text;
    std::string_view algorithm = "bfs";

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--algorithm" || args[i] == "-a") {
            if (++i == args.size()) {
                die("--algorithm needs a value (bfs, ida or table)", 2);
            }
            algorithm = args[i];
        } else if (args[i].starts_with("--algorithm=")) {
            algorithm = args[i].substr(std::string_view{"--algorithm="}.size());
        } else if (!text) {
            text = args[i];
        } else {
            die(std::format("unexpected argument '{}'", args[i]), 2);
        }
    }
    if (!text) {
        die("solve needs a state (24 colour letters, or '-' for stdin)", 2);
    }

    const Result<State> state = state_argument(*text);
    if (!state) {
        return report(state.error());
    }

    Result<Moves> solution = fail(ErrorKind::usage, "");
    if (algorithm == "bfs") {
        solution = solve_bfs(*state);
    } else if (algorithm == "ida") {
        solution = solve_ida(*state);
    } else if (algorithm == "table") {
        solution = DistanceTable::build().solve(*state);
    } else {
        die(std::format("unknown algorithm '{}' (bfs, ida or table)", algorithm), 2);
    }

    if (!solution) {
        return report(solution.error());
    }
    if (solution->empty()) {
        std::cerr << "already solved\n";
    } else {
        std::println("{}", *solution);
    }
    return 0;
}

int command_scramble(std::span<const std::string_view> args) {
    int moves = 8;
    std::uint64_t seed = std::random_device{}();

    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool is_count = args[i] == "-n" || args[i] == "--moves";
        const bool is_seed = args[i] == "--seed";
        if (!is_count && !is_seed) {
            die(std::format("unexpected argument '{}'", args[i]), 2);
        }
        if (++i == args.size()) {
            die(std::format("'{}' needs a value", args[i - 1]), 2);
        }
        const std::optional<long> value = to_number(args[i]);
        if (!value || *value < 0) {
            die(std::format("'{}' is not a non-negative number", args[i]), 2);
        }
        if (is_count) {
            moves = static_cast<int>(*value);
        } else {
            seed = static_cast<std::uint64_t>(*value);
        }
    }

    std::println("{}", to_string(scramble(solved, moves, seed)));
    return 0;
}

int command_show(std::span<const std::string_view> args) {
    std::optional<std::string_view> text;
    std::optional<display::ColorMode> color;

    for (std::string_view argument : args) {
        if (argument == "--color") {
            color = display::best_color();
        } else if (argument == "--no-color") {
            color = display::ColorMode::none;
        } else if (!text) {
            text = argument;
        } else {
            die(std::format("unexpected argument '{}'", argument), 2);
        }
    }

    const Result<State> state = text ? state_argument(*text) : Result<State>{solved};
    if (!state) {
        return report(state.error());
    }
    std::println("{}", display::render(*state, color.value_or(display::detect_color())));
    return 0;
}

int command_table(std::span<const std::string_view> args) {
    std::optional<long> workers;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--workers") {
            die(std::format("unexpected argument '{}'", args[i]), 2);
        }
        if (++i == args.size()) {
            die("--workers needs a value", 2);
        }
        workers = to_number(args[i]);
        if (!workers || *workers < 1) {
            die(std::format("'{}' is not a positive number", args[i]), 2);
        }
    }

    const auto started = std::chrono::steady_clock::now();
    const DistanceTable table = workers ? DistanceTable::build_parallel(
                                              static_cast<unsigned>(*workers))
                                        : DistanceTable::build();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    const std::vector<std::uint64_t> histogram = table.histogram();
    std::string line;
    std::uint64_t total = 0;
    for (std::uint64_t count : histogram) {
        line += line.empty() ? "" : ", ";
        line += std::format("{}", count);
        total += count;
    }

    std::println("built {} states in {:.3f} s, {:.1f} MB resident", total, seconds,
                 static_cast<double>(table.data().size()) / 1e6);
    std::println("{}", line);
    const bool matches = std::ranges::equal(histogram, oeis_a079762);
    std::println("{}", matches ? "matches OEIS A079762, the published distance distribution"
                               : "DOES NOT MATCH OEIS A079762 -- the move tables or the encoding "
                                 "are wrong");
    return matches ? 0 : 1;
}

int command_bench(std::span<const std::string_view> args) {
    bool json = false;
    std::optional<std::string> path;
    std::optional<long> limit;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") {
            json = true;
        } else if (args[i] == "--scrambles") {
            if (++i == args.size()) {
                die("--scrambles needs a file", 2);
            }
            path = std::string{args[i]};
        } else if (args[i].starts_with("--scrambles=")) {
            path = std::string{args[i].substr(std::string_view{"--scrambles="}.size())};
        } else if (args[i] == "-n" || args[i] == "--samples") {
            if (++i == args.size()) {
                die("-n needs a value", 2);
            }
            limit = to_number(args[i]);
            if (!limit || *limit <= 0) {
                die(std::format("'{}' is not a positive number", args[i]), 2);
            }
        } else {
            die(std::format("unexpected argument '{}'", args[i]), 2);
        }
    }

    std::vector<State> scrambles;
    if (!path) {
        path = bench::find_scrambles();
    }
    if (path) {
        Result<std::vector<State>> loaded = bench::read_scrambles(*path);
        if (!loaded) {
            return report(loaded.error());
        }
        scrambles = std::move(*loaded);
    } else {
        // The published scrambles keep every language measuring the same work;
        // falling back to random ones is better than refusing to run.
        const long count = limit.value_or(200);
        std::cerr << std::format(
            "rubik2x2: bench/scrambles.txt not found, using {} random scrambles\n", count);
        for (long i = 0; i < count; ++i) {
            scrambles.push_back(scramble(solved, 14, static_cast<std::uint64_t>(i)));
        }
    }
    if (limit && static_cast<std::size_t>(*limit) < scrambles.size()) {
        scrambles.resize(static_cast<std::size_t>(*limit));
    }

    const bench::Report result = bench::run(scrambles);
    std::println("{}", json ? bench::to_json(result) : bench::to_text(result));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    if (arguments.empty() || arguments[0] == "--help" || arguments[0] == "-h" ||
        arguments[0] == "help") {
        std::print("{}", usage_text);
        return arguments.empty() ? 2 : 0;
    }

    const std::span<const std::string_view> rest{arguments.begin() + 1, arguments.end()};
    const std::string_view command = arguments[0];

    if (command == "solve") {
        return command_solve(rest);
    }
    if (command == "scramble") {
        return command_scramble(rest);
    }
    if (command == "show") {
        return command_show(rest);
    }
    if (command == "table") {
        return command_table(rest);
    }
    if (command == "bench") {
        return command_bench(rest);
    }

    std::cerr << std::format("rubik2x2: unknown command '{}'; try --help\n", command);
    return 2;
}
