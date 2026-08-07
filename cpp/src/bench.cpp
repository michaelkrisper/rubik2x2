#include "rubik/bench.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>

#include "rubik/bfs.hpp"
#include "rubik/coords.hpp"
#include "rubik/ida.hpp"
#include "rubik/memtrack.hpp"
#include "rubik/table.hpp"

#ifndef RUBIK_BUILD_FLAGS
#define RUBIK_BUILD_FLAGS "unspecified"
#endif

namespace rubik::bench {
namespace {

using Clock = std::chrono::steady_clock;
using Solver = std::function<Result<Moves>(const State&)>;

/// Keeps the optimiser from deleting a solve whose result is otherwise unused.
std::size_t sink = 0;

[[nodiscard]] double elapsed_ms(Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

/// Time one call, then repeat it enough times to fill roughly `target_ms`.
/// A single table solve takes a couple of microseconds; without repetition the
/// clock would be measuring itself.
template <typename Body>
[[nodiscard]] double measure_once_ms(Body&& body, double target_ms) {
    const Clock::time_point started = Clock::now();
    body();
    const double first = elapsed_ms(started);
    if (first >= target_ms) {
        return first;
    }
    const int repeats =
        std::clamp(static_cast<int>(target_ms / std::max(first, 1e-6)), 1, 1'000'000);
    const Clock::time_point again = Clock::now();
    for (int i = 0; i < repeats; ++i) {
        body();
    }
    return elapsed_ms(again) / repeats;
}

/// The best of several timings. Interference from the rest of the machine can
/// only ever make a measurement slower, so the minimum is the estimate closest
/// to what the code costs on its own.
template <typename Body>
[[nodiscard]] double measure_ms(Body&& body, double target_ms, int samples = 3) {
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; ++i) {
        best = std::min(best, measure_once_ms(body, target_ms));
    }
    return best;
}

[[nodiscard]] std::size_t solve_all(const Solver& solve, std::span<const State> states) {
    std::size_t total = 0;
    for (const State& state : states) {
        total += solve(state).value_or(Moves{}).size();
    }
    return total;
}

/// Every solution must be as short as the distance table says is possible, and
/// must actually solve the cube it came from.
[[nodiscard]] bool all_optimal(const Solver& solve, std::span<const State> states,
                               const DistanceTable& reference) {
    for (const State& state : states) {
        const Result<Moves> solution = solve(state);
        const Result<std::uint32_t> index = coords::encode(state);
        if (!solution || !index) {
            return false;
        }
        if (static_cast<int>(solution->size()) != reference.distance(*index)) {
            return false;
        }
        const Result<State> finished = turn(state, *solution);
        if (!finished || !is_solved(*finished)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] AlgorithmResult evaluate(std::string name, const std::function<void()>& setup,
                                       const Solver& solve, std::span<const State> scrambles,
                                       const State& hardest, const DistanceTable& reference) {
    AlgorithmResult result{.name = std::move(name), .workers = std::nullopt};

    // Memory is what the algorithm still holds after its setup (the pattern
    // databases, the distance table) plus the high-water mark of one hard
    // solve, which is where the transient frontiers are widest. Scratch space
    // the setup allocates and frees again is deliberately not counted: it is
    // not held while solving, which is what the figure is meant to describe.
    memtrack::reset();
    result.setup_ms = setup ? measure_ms(setup, 0.0) : 0.0;
    const std::size_t retained = memtrack::live_bytes();

    memtrack::reset();
    sink += solve(hardest).value_or(Moves{}).size();
    result.peak_mb = static_cast<double>(retained + memtrack::peak_bytes()) / 1e6;

    result.avg_ms = measure_ms([&] { sink += solve_all(solve, scrambles); }, 200.0) /
                    static_cast<double>(scrambles.size());
    result.worst_ms = measure_ms([&] { sink += solve(hardest).value_or(Moves{}).size(); }, 100.0);
    result.optimal = all_optimal(solve, scrambles, reference);
    return result;
}

[[nodiscard]] std::string standard_label() {
#if __cplusplus >= 202302L
    return "C++23";
#else
    return std::format("C++ (__cplusplus={})", __cplusplus);
#endif
}

[[nodiscard]] std::string compiler_label() {
#if defined(__clang__)
    return std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    return std::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    return "unknown compiler";
#endif
}

/// JSON numbers, short but never in a form JSON rejects.
[[nodiscard]] std::string number(double value) {
    std::string text = std::format("{:.6g}", value);
    if (text.find_first_of(".e") == std::string::npos) {
        text += ".0";
    }
    return text;
}

}  // namespace

Result<std::vector<State>> read_scrambles(std::string_view path) {
    std::ifstream file{std::filesystem::path{path}};
    if (!file) {
        return fail(ErrorKind::usage, std::format("cannot read '{}'", path));
    }

    std::vector<State> states;
    std::string line;
    int number_of_line = 0;
    while (std::getline(file, line)) {
        ++number_of_line;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        Result<State> state = parse(line);
        if (!state) {
            return fail(state.error().kind,
                        std::format("{}:{}: {}", path, number_of_line, state.error().message));
        }
        states.push_back(*state);
    }
    if (states.empty()) {
        return fail(ErrorKind::usage, std::format("'{}' holds no scrambles", path));
    }
    return states;
}

std::optional<std::string> find_scrambles() {
    // Run from cpp/, from the repository root, or from a build directory.
    for (std::string_view candidate :
         {"bench/scrambles.txt", "../bench/scrambles.txt", "../../bench/scrambles.txt"}) {
        if (std::filesystem::exists(candidate)) {
            return std::string{candidate};
        }
    }
    return std::nullopt;
}

Report run(std::span<const State> scrambles) {
    // Shared by every algorithm and built once, so it belongs to none of them.
    [[maybe_unused]] const coords::MoveTables& shared = coords::move_tables();

    const State hardest = parse(hardest_scramble).value();
    const DistanceTable reference = DistanceTable::build();

    Report report{.language = "C++",
                  .version = std::format("{} ({})", standard_label(), compiler_label()),
                  .build = RUBIK_BUILD_FLAGS,
                  .algorithms = {}};

    // An empty setup, so bidirectional search reports 0.0 rather than the cost
    // of calling an empty function.
    report.algorithms.push_back(evaluate(
        "bfs", {}, [](const State& state) { return solve_bfs(state); }, scrambles, hardest,
        reference));

    PatternDatabases databases{};
    report.algorithms.push_back(evaluate(
        "ida", [&] { databases = PatternDatabases::build(); },
        [&](const State& state) { return solve_ida(state, databases); }, scrambles, hardest,
        reference));

    std::optional<DistanceTable> table;
    const AlgorithmResult serial = evaluate(
        "table", [&] { table = DistanceTable::build(); },
        [&](const State& state) { return table->solve(state); }, scrambles, hardest, reference);
    report.algorithms.push_back(serial);

    // Only the build is parallelised, so every per-solve figure carries over
    // unchanged; measuring them again would only add noise.
    const unsigned workers = DistanceTable::default_workers();
    AlgorithmResult parallel = serial;
    parallel.name = "table-par";
    parallel.workers = workers;
    {
        std::optional<DistanceTable> built;
        parallel.setup_ms = measure_ms([&] { built = DistanceTable::build_parallel(workers); }, 0.0);
        // Cheap insurance that the two builds really do agree.
        parallel.optimal = serial.optimal && std::ranges::equal(built->data(), table->data());
    }
    report.algorithms.push_back(parallel);

    return report;
}

std::string to_json(const Report& report) {
    std::string json = std::format(
        "{{\"language\": \"{}\", \"version\": \"{}\", \"build\": \"{}\", \"algorithms\": [",
        report.language, report.version, report.build);
    for (std::size_t i = 0; i < report.algorithms.size(); ++i) {
        const AlgorithmResult& result = report.algorithms[i];
        json += std::format(
            "{}{{\"name\": \"{}\", \"setup_ms\": {}, \"avg_ms\": {}, \"worst_ms\": {}, "
            "\"peak_mb\": {}, \"optimal\": {}",
            i == 0 ? "" : ", ", result.name, number(result.setup_ms), number(result.avg_ms),
            number(result.worst_ms), number(result.peak_mb), result.optimal ? "true" : "false");
        if (result.workers) {
            json += std::format(", \"workers\": {}", *result.workers);
        }
        json += "}";
    }
    json += "]}";
    return json;
}

std::string to_text(const Report& report) {
    std::string text = std::format("{} -- {}\nbuild: {}\n\n", report.language, report.version,
                                   report.build);
    text += std::format("{:<11}{:>11}{:>10}{:>11}{:>10}{:>9}\n", "algorithm", "setup ms", "avg ms",
                        "worst ms", "peak MB", "optimal");
    text += std::string(62, '-') + "\n";
    for (const AlgorithmResult& result : report.algorithms) {
        text += std::format("{:<11}{:>11.1f}{:>10.4g}{:>11.4g}{:>10.1f}{:>9}\n", result.name,
                            result.setup_ms, result.avg_ms, result.worst_ms, result.peak_mb,
                            result.optimal ? "yes" : "NO");
    }

    text += "\ntotal time for a workload (setup + solves), seconds\n";
    constexpr std::array<int, 5> counts{1, 10, 100, 1'000, 10'000};
    text += std::format("{:<11}", "algorithm");
    for (int count : counts) {
        text += std::format("{:>12}", count);
    }
    text += "\n" + std::string(11 + 12 * counts.size(), '-') + "\n";
    for (const AlgorithmResult& result : report.algorithms) {
        text += std::format("{:<11}", result.name);
        for (int count : counts) {
            text += std::format("{:>12.2f}",
                                (result.setup_ms + count * result.avg_ms) / 1000.0);
        }
        text += "\n";
    }

    const auto by_name = [&](std::string_view name) {
        return *std::ranges::find(report.algorithms, name, &AlgorithmResult::name);
    };
    const AlgorithmResult& table = by_name("table");
    const AlgorithmResult& bfs = by_name("bfs");
    const double gain = bfs.avg_ms - table.avg_ms;
    if (gain > 0.0) {
        const long crossover = static_cast<long>((table.setup_ms - bfs.setup_ms) / gain) + 1;
        text += std::format(
            "\nThe table overtakes bidirectional search at about {} solves. Below that,\n"
            "building it costs more than it saves; above it, nothing else comes close.\n",
            crossover);
    }
    return text;
}

}  // namespace rubik::bench
