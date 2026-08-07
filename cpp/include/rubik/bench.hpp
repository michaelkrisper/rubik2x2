/// Compare the solvers, setup cost included.
///
/// Per-solve time alone flatters the lookup table, because it hides the
/// exhaustive search that produced it. What decides which algorithm to reach
/// for is total time for the workload you have -- so setup, per-solve and
/// worst case are all reported, and the text report adds the break-even point.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "rubik/cube.hpp"
#include "rubik/error.hpp"

namespace rubik::bench {

/// An antipodal position: 14 quarter turns from solved, the hardest there is.
inline constexpr std::string_view hardest_scramble = "wppryygrygrwgprbbbwpbwyg";

struct AlgorithmResult {
    std::string name;
    double setup_ms = 0.0;
    double avg_ms = 0.0;
    double worst_ms = 0.0;
    double peak_mb = 0.0;
    bool optimal = false;
    /// Only `table-par` reports this.
    std::optional<unsigned> workers;
};

struct Report {
    std::string language;
    std::string version;
    std::string build;
    std::vector<AlgorithmResult> algorithms;
};

/// One state string per line; blank lines and `#` comments are skipped.
[[nodiscard]] Result<std::vector<State>> read_scrambles(std::string_view path);

/// Where `bench/scrambles.txt` lives relative to a few plausible working
/// directories, or nothing if it cannot be found.
[[nodiscard]] std::optional<std::string> find_scrambles();

[[nodiscard]] Report run(std::span<const State> scrambles);

[[nodiscard]] std::string to_json(const Report& report);
[[nodiscard]] std::string to_text(const Report& report);

}  // namespace rubik::bench
